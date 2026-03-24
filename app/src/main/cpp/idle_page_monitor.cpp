//
// idle_page_monitor.cpp
// Idle Page 监控主类实现
//

#include "idle_page_monitor.h"
#include "idle_page_log.h"

#include <fcntl.h>
#include <unistd.h>
#include <chrono>
#include <cstring>
#include <algorithm>

namespace idle_page {

// ==================== 单例 ====================

IdlePageMonitor& IdlePageMonitor::instance() {
    static IdlePageMonitor inst;
    return inst;
}

IdlePageMonitor::~IdlePageMonitor() {
    shutdown();
}

// ==================== 初始化 ====================

bool IdlePageMonitor::init(const char* so_name, const char* log_path, int initial_interval_ms) {
    if (running_) {
        IDLE_LOGE("Monitor already running");
        return false;
    }

    so_name_ = so_name;
    log_path_ = log_path;

    // 1. 初始化节区解析器
    if (!section_resolver_.init(so_name)) {
        IDLE_LOGE("Failed to init section resolver for %s", so_name);
        return false;
    }

    // 2. 打开 pagemap
    if (!pagemap_.open()) {
        IDLE_LOGE("Failed to open pagemap");
        return false;
    }

    // 3. 打开 page_idle
    if (!page_idle_.open()) {
        IDLE_LOGE("Failed to open page_idle/bitmap");
        pagemap_.close();
        return false;
    }

    // 4. 打开日志文件
    log_fd_ = ::open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (log_fd_ < 0) {
        IDLE_LOGE("Failed to open log: %s", log_path);
        page_idle_.close();
        pagemap_.close();
        return false;
    }

    // 5. 写入日志头
    write_log_header();

    // 6. 初始化定时器
    timer_.init(initial_interval_ms, [this]() {
        // 定时器回调：投递任务
        uint64_t seq = sequence_id_.fetch_add(1);
        uint64_t now = get_timestamp_us();

        SampleTask task;
        task.timestamp_us = now;
        task.sequence_id = seq;

        if (seq % 2 == 0) {
            // 偶数：START 任务
            task.type = TaskType::SAMPLE_START;
            task.monitor_start = target_start_;
            task.monitor_end = target_end_;
        } else {
            // 奇数：END 任务
            task.type = TaskType::SAMPLE_END;
            task.monitor_start = 0;
            task.monitor_end = 0;
        }

        task_queue_.enqueue(task);
    });

    IDLE_LOGI("IdlePageMonitor initialized");
    IDLE_LOGI("  Target: %s", so_name);
    IDLE_LOGI("  Log: %s", log_path);
    IDLE_LOGI("  Initial interval: %dms", initial_interval_ms);

    return true;
}

void IdlePageMonitor::set_target_range(uintptr_t start, uintptr_t end) {
    target_start_ = start;
    target_end_ = end;
    IDLE_LOGI("Target range: 0x%llx - 0x%llx", (unsigned long long)start, (unsigned long long)end);
}

// ==================== 启停控制 ====================

void IdlePageMonitor::start() {
    if (running_) return;

    // 自动检测目标范围（如果未设置）
    if (target_start_ == 0 || target_end_ == 0) {
        std::vector<MemoryRegion> regions;
        if (ProcMapsParser::find_so_regions(so_name_.c_str(), regions)) {
            // 合并所有区域
            target_start_ = regions[0].start;
            target_end_ = regions[0].end;
            for (const auto& r : regions) {
                if (r.start < target_start_) target_start_ = r.start;
                if (r.end > target_end_) target_end_ = r.end;
            }
            IDLE_LOGI("Auto-detected range: 0x%llx - 0x%llx",
                     (unsigned long long)target_start_, (unsigned long long)target_end_);
        }
    }

    // 启动工作线程
    worker_running_ = true;
    worker_thread_ = std::thread(&IdlePageMonitor::worker_loop, this);

    // 启动定时器
    timer_.start();
    running_ = true;

    IDLE_LOGI("Monitoring started");
}

void IdlePageMonitor::stop() {
    if (!running_) return;

    running_ = false;
    timer_.stop();

    // 等待工作线程结束
    worker_running_ = false;
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    // 刷新日志
    flush_log_buffer();

    IDLE_LOGI("Monitoring stopped");
}

void IdlePageMonitor::shutdown() {
    stop();

    // 关闭文件
    if (log_fd_ >= 0) {
        flush_log_buffer();
        ::close(log_fd_);
        log_fd_ = -1;
    }

    page_idle_.close();
    pagemap_.close();

    // 清空缓存
    page_cache_.clear();
    pfn_list_.clear();

    IDLE_LOGI("Monitor shutdown complete");
}

// ==================== 工作线程 ====================

void IdlePageMonitor::worker_loop() {
    IDLE_LOGI("Worker thread started");

    SampleTask task;
    while (worker_running_) {
        if (task_queue_.dequeue(task)) {
            if (task.type == TaskType::SHUTDOWN) {
                break;
            }
            execute_task(task);
        } else {
            usleep(100);  // 100us 避免忙等
        }
    }

    // 处理剩余任务
    while (task_queue_.dequeue(task)) {
        if (task.type != TaskType::SHUTDOWN) {
            execute_task(task);
        }
    }

    IDLE_LOGI("Worker thread stopped");
}

void IdlePageMonitor::execute_task(const SampleTask& task) {
    switch (task.type) {
        case TaskType::SAMPLE_START:
            current_sequence_ = task.sequence_id;
            do_sample_start(task.monitor_start, task.monitor_end);
            break;

        case TaskType::SAMPLE_END:
            if (task.sequence_id == current_sequence_) {
                do_sample_end();
            }
            break;

        default:
            break;
    }
}

// ==================== 采样操作 ====================

bool IdlePageMonitor::init_page_cache(uintptr_t start, uintptr_t end) {
    page_cache_.clear();
    pfn_list_.clear();

    if (start == 0 || end == 0 || end <= start) {
        IDLE_LOGE("Invalid range: 0x%llx - 0x%llx", (unsigned long long)start, (unsigned long long)end);
        return false;
    }

    size_t page_count = (end - start + 4095) / 4096;
    page_cache_.reserve(page_count);
    pfn_list_.reserve(page_count);

    for (uintptr_t addr = start; addr < end; addr += 4096) {
        uint64_t pfn = pagemap_.get_pfn(addr);

        PageInfo info;
        info.vaddr = addr;
        info.pfn = pfn;
        info.is_accessible = (pfn != 0);

        // 查找节区信息
        // 这里简化为只存储索引，实际使用时再查询
        info.region_idx = 0;

        page_cache_.push_back(info);

        if (pfn != 0) {
            pfn_list_.push_back(pfn);
        }
    }

    // 预分配结果数组
    access_results_.resize(pfn_list_.size());

    IDLE_LOGI("Page cache initialized: %zu pages (%zu valid PFNs)",
             page_cache_.size(), pfn_list_.size());

    stats_.total_pages = page_cache_.size();

    return !page_cache_.empty();
}

void IdlePageMonitor::do_sample_start(uintptr_t start, uintptr_t end) {
    // 首次采样：构建缓存
    if (first_sample_.exchange(false)) {
        if (!init_page_cache(start, end)) {
            IDLE_LOGE("Failed to init page cache");
            return;
        }
    }

    // 设置所有页为 idle 状态
    // 清除 PTE Accessed bit，准备下一周期的检测
    for (const auto& page : page_cache_) {
        if (page.is_accessible && page.pfn != 0) {
            page_idle_.set_idle(page.pfn);
        }
    }
}

void IdlePageMonitor::do_sample_end() {
    if (page_cache_.empty()) return;

    uint64_t timestamp = get_timestamp_us();
    size_t pfn_idx = 0;
    size_t accessed_count = 0;

    // 批量检查访问状态
    for (const auto& page : page_cache_) {
        bool was_accessed = false;

        if (page.is_accessible && page.pfn != 0) {
            was_accessed = page_idle_.is_accessed(page.pfn);
            pfn_idx++;
        }

        if (was_accessed) {
            accessed_count++;
        }

        // 获取节区信息
        std::string section = section_resolver_.resolve(page.vaddr);

        // 写入日志
        // 格式: timestamp_us,sequence,vaddr,pfn,accessed,section
        int n = snprintf(log_buffer_ + log_offset_,
                        LOG_BUFFER_SIZE - log_offset_,
                        "%llu,%llu,0x%llx,%llu,%d,%s\n",
                        (unsigned long long)timestamp,
                        (unsigned long long)current_sequence_,
                        (unsigned long long)page.vaddr,
                        (unsigned long long)page.pfn,
                        was_accessed ? 1 : 0,
                        section.c_str());

        if (n > 0) {
            log_offset_ += n;
        }

        // 缓冲区满时刷新
        if (log_offset_ >= LOG_BUFFER_SIZE - 256) {
            flush_log_buffer();
        }
    }

    // 刷新日志
    flush_log_buffer();

    // 更新统计
    stats_.total_samples++;
    stats_.accessed_pages += accessed_count;
    float ratio = page_cache_.empty() ? 0.0f :
                  (float)accessed_count / page_cache_.size();
    stats_.avg_access_ratio = ratio;
    stats_.current_interval_ms = timer_.get_interval_ms();

    // 动态调整频率
    timer_.auto_adjust_rate(ratio);

    // 每 10 个周期输出一次统计
    if (current_sequence_ % 10 == 0) {
        IDLE_LOGI("Sample #%llu: accessed %zu/%zu (%.1f%%)",
                 (unsigned long long)current_sequence_,
                 accessed_count, page_cache_.size(), ratio * 100);
    }
}

// ==================== 日志 ====================

void IdlePageMonitor::write_log_header() {
    const char* header = "# mem_visit.log - Idle Page Tracking\n"
                         "# Format: timestamp_us,sequence,vaddr,pfn,accessed,section\n"
                         "# accessed: 1=accessed, 0=idle\n";

    if (log_fd_ >= 0) {
        write(log_fd_, header, strlen(header));
    }
}

void IdlePageMonitor::flush_log_buffer() {
    if (log_offset_ > 0 && log_fd_ >= 0) {
        write(log_fd_, log_buffer_, log_offset_);
        log_offset_ = 0;
    }
}

// ==================== 工具 ====================

uint64_t IdlePageMonitor::get_timestamp_us() {
    using namespace std::chrono;
    return duration_cast<microseconds>(
        steady_clock::now().time_since_epoch()).count();
}

IdlePageMonitor::Stats IdlePageMonitor::get_stats() const {
    return stats_;
}

// ==================== C 接口 ====================

bool idle_page_monitor_init(const char* so_name, const char* log_path) {
    // 使用 100ms 默认周期
    return IdlePageMonitor::instance().init(so_name, log_path, 100);
}

void idle_page_monitor_start() {
    IdlePageMonitor::instance().start();
}

void idle_page_monitor_stop() {
    IdlePageMonitor::instance().stop();
}

void idle_page_monitor_shutdown() {
    IdlePageMonitor::instance().shutdown();
}

} // namespace idle_page
