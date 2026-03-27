//
// idle_page_monitor.cpp
// Idle Page 监控主类实现 - 支持多区域监控
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

bool IdlePageMonitor::init(MonitorMode mode, const char* so_name, const char* log_path, int initial_interval_ms) {
    if (running_) {
        IDLE_LOGE("Monitor already running");
        return false;
    }

    mode_ = mode;
    so_name_ = so_name ? so_name : "";
    log_path_ = log_path;

    // 1. 打开日志文件
    log_fd_ = ::open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (log_fd_ < 0) {
        IDLE_LOGE("Failed to open log: %s", log_path);
        return false;
    }

    // 2. 写入日志头
    write_log_header();

    // 3. 初始化定时器（但先不启动）
    timer_.init(initial_interval_ms, [this]() {
        // 定时器回调：投递任务
        uint64_t seq = sequence_id_.fetch_add(1);
        uint64_t now = get_timestamp_us();

        SampleTask task;
        task.timestamp_us = now;
        // START 和 END 共享同一个 sequence_id（每两个任务一个周期）
        task.sequence_id = seq / 2;

        if (seq % 2 == 0) {
            // 偶数：START 任务
            task.type = TaskType::SAMPLE_START;
        } else {
            // 奇数：END 任务
            task.type = TaskType::SAMPLE_END;
        }

        task_queue_.enqueue(task);
    });

    IDLE_LOGI("IdlePageMonitor initialized");
    IDLE_LOGI("  Mode: %s", mode_ == MonitorMode::SO_CODE_SECTIONS ? "SO_CODE_SECTIONS" : "HEAP_ALLOCATIONS");
    IDLE_LOGI("  Target: %s", so_name_.empty() ? "(none)" : so_name_.c_str());
    IDLE_LOGI("  Log: %s", log_path);
    IDLE_LOGI("  Initial interval: %dms", initial_interval_ms);

    return true;
}

void IdlePageMonitor::add_target_region(const MemoryRegion& region) {
    target_regions_.push_back(region);
    IDLE_LOGI("Added target region: 0x%llx - 0x%llx (%s)",
              (unsigned long long)region.start, (unsigned long long)region.end,
              region.perms);
}

void IdlePageMonitor::set_target_regions(const std::vector<MemoryRegion>& regions) {
    target_regions_ = regions;
    IDLE_LOGI("Set %zu target regions", target_regions_.size());
    for (const auto& r : target_regions_) {
        IDLE_LOGI("  0x%llx - 0x%llx (%s)",
                  (unsigned long long)r.start, (unsigned long long)r.end,
                  r.perms);
    }
}

// ==================== 启停控制 ====================

void IdlePageMonitor::start() {
    if (running_) return;

    // 预检查 root 权限
    bool has_root = MmapPageIdle::check_root_access();
    if (!has_root) {
        IDLE_LOGE("==========================================");
        IDLE_LOGE("Root permission required for Idle Page Monitor!");
        IDLE_LOGE("Please run with: adb shell su -c 'am start ...'");
        IDLE_LOGE("Or use: adb shell su -c '/data/app/.../libso2.so'");
        IDLE_LOGE("==========================================");
    }

    // 1. 打开 pagemap
    if (!pagemap_.open()) {
        IDLE_LOGE("Failed to open pagemap");
        return;
    }

    // 2. 打开 page_idle（需要 root）
    bool page_idle_available = page_idle_.open();
    if (!page_idle_available) {
        IDLE_LOGI("page_idle/bitmap not available (no root), running in degraded mode");
    }

    // 3. 加载运行时内存区域（从 /proc/self/maps 读取）
    if (mode_ == MonitorMode::SO_CODE_SECTIONS) {
        // SO模式：从 maps 加载 SO 区域
        if (!ProcMapsParser::find_so_regions(so_name_.c_str(), target_regions_)) {
            IDLE_LOGE("Failed to find SO regions for %s", so_name_.c_str());
            page_idle_.close();
            pagemap_.close();
            return;
        }
        if (target_regions_.empty()) {
            IDLE_LOGE("No regions found for %s", so_name_.c_str());
            page_idle_.close();
            pagemap_.close();
            return;
        }
    }
    // 堆模式：不需要预加载区域，由 track_allocation 动态添加

    // 输出区域信息
    IDLE_LOGI("Monitoring %zu regions:", target_regions_.size());
    size_t total_pages = 0;
    for (const auto& r : target_regions_) {
        size_t pages = (r.end - r.start + 4095) / 4096;
        total_pages += pages;
        IDLE_LOGI("  0x%llx - 0x%llx (%s): %zu pages",
                  (unsigned long long)r.start, (unsigned long long)r.end,
                  r.perms, pages);
    }
    IDLE_LOGI("Total: %zu pages (~%.2f MB)",
              total_pages, total_pages * 4096.0 / (1024 * 1024));

    // 4. 启动工作线程
    worker_running_ = true;
    worker_thread_ = std::thread(&IdlePageMonitor::worker_loop, this);

    // 5. 启动定时器
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
    target_regions_.clear();

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
            IDLE_LOGI("[IdlePage] SAMPLE_START #%llu (%zu regions)",
                      (unsigned long long)task.sequence_id, target_regions_.size());
            current_sequence_ = task.sequence_id;
            do_sample_start_all();
            break;

        case TaskType::SAMPLE_END:
            IDLE_LOGI("[IdlePage] SAMPLE_END #%llu", (unsigned long long)task.sequence_id);
            if (task.sequence_id == current_sequence_) {
                do_sample_end_all();
            } else {
                IDLE_LOGI("[IdlePage] SAMPLE_END sequence mismatch: task=%llu, current=%llu",
                          (unsigned long long)task.sequence_id, (unsigned long long)current_sequence_);
            }
            break;

        case TaskType::ADD_REGION:
            handle_add_region(task.region_start, task.region_end, task.region_flags);
            break;

        default:
            IDLE_LOGI("[IdlePage] Unknown task type");
            break;
    }
}

// ==================== 采样操作（多区域） ====================

bool IdlePageMonitor::init_page_cache_for_region(const MemoryRegion& region) {
    // 清空当前缓存，为该区域重新构建
    page_cache_.clear();
    pfn_list_.clear();

    size_t page_count = (region.end - region.start + 4095) / 4096;
    if (page_count == 0) {
        IDLE_LOGE("Empty region: 0x%llx - 0x%llx",
                  (unsigned long long)region.start, (unsigned long long)region.end);
        return false;
    }

    page_cache_.reserve(page_count);
    pfn_list_.reserve(page_count);

    for (uintptr_t addr = region.start; addr < region.end; addr += 4096) {
        uint64_t pfn = pagemap_.get_pfn(addr);

        PageInfo info;
        info.vaddr = addr;
        info.pfn = pfn;
        info.is_accessible = true;
        info.region_idx = 0;  // 不需要索引，直接从 region 获取权限

        page_cache_.push_back(info);

        if (pfn != 0) {
            pfn_list_.push_back(pfn);
        }
    }

    access_results_.resize(pfn_list_.size());

    IDLE_LOGI("Region cache: 0x%llx-0x%llx (%s): %zu pages (%zu valid PFNs)",
              (unsigned long long)region.start, (unsigned long long)region.end,
              region.perms, page_cache_.size(), pfn_list_.size());

    return !page_cache_.empty();
}

void IdlePageMonitor::do_sample_start_all() {
    // 为每个区域设置 idle 状态
    // 注意：我们需要为每个区域单独处理，但 page_idle 操作是针对 PFN 的
    // 优化：收集所有 PFN，批量设置 idle

    std::vector<uint64_t> all_pfns;
    all_pfns.reserve(1024);

    for (const auto& region : target_regions_) {
        // 临时收集该区域的 PFN
        std::vector<uint64_t> region_pfns;
        for (uintptr_t addr = region.start; addr < region.end; addr += 4096) {
            uint64_t pfn = pagemap_.get_pfn(addr);
            if (pfn != 0) {
                region_pfns.push_back(pfn);
            }
        }

        // 批量设置该区域的页为 idle
        for (uint64_t pfn : region_pfns) {
            page_idle_.set_idle(pfn);
        }

        IDLE_LOGD("Set %zu pages to idle for region 0x%llx-0x%llx",
                  region_pfns.size(),
                  (unsigned long long)region.start, (unsigned long long)region.end);
    }
}

void IdlePageMonitor::do_sample_end_all() {
    uint64_t timestamp = get_timestamp_us();
    size_t total_pages = 0;
    size_t total_accessed = 0;

    // 分别处理每个区域
    for (const auto& region : target_regions_) {
        size_t region_pages = 0;
        size_t region_accessed = 0;

        for (uintptr_t addr = region.start; addr < region.end; addr += 4096) {
            uint64_t pfn = pagemap_.get_pfn(addr);
            int accessed_status = 0;

            if (pfn != 0) {
                bool was_accessed = page_idle_.is_accessed(pfn);
                accessed_status = was_accessed ? 1 : 0;
                if (was_accessed) {
                    region_accessed++;
                }
            } else {
                accessed_status = -1;  // unknown
            }
            region_pages++;

            // 写入日志 - 根据模式选择显示格式
            const char* region_label;
            char label_buffer[128];
            if (mode_ == MonitorMode::HEAP_ALLOCATIONS) {
                // 堆模式：显示 (heap)
                region_label = !region.name.empty() ? region.name.c_str() : region.perms;
            } else {
                // SO模式：显示 权限(文件名)
                const char* name = region.name.empty() ? "" : region.name.c_str();
                // 提取文件名（不含路径）
                const char* last_slash = strrchr(name, '/');
                const char* filename = last_slash ? last_slash + 1 : name;
                snprintf(label_buffer, sizeof(label_buffer), "%s(%s)",
                         region.perms, filename);
                region_label = label_buffer;
            }
            int n = snprintf(log_buffer_ + log_offset_,
                            LOG_BUFFER_SIZE - log_offset_,
                            "%llu,%llu,0x%llx,%llu,%d,(%s)\n",
                            (unsigned long long)timestamp,
                            (unsigned long long)current_sequence_,
                            (unsigned long long)addr,
                            (unsigned long long)pfn,
                            accessed_status,
                            region_label);

            if (n > 0) {
                log_offset_ += n;
            }

            // 缓冲区满时刷新
            if (log_offset_ >= LOG_BUFFER_SIZE - 256) {
                flush_log_buffer();
            }
        }

        total_pages += region_pages;
        total_accessed += region_accessed;

        IDLE_LOGD("Region 0x%llx-0x%llx: %zu/%zu accessed",
                  (unsigned long long)region.start, (unsigned long long)region.end,
                  region_accessed, region_pages);
    }

    // 刷新日志
    flush_log_buffer();

    // 更新统计
    stats_.total_samples++;
    stats_.accessed_pages += total_accessed;
    stats_.total_pages = total_pages;
    float ratio = total_pages > 0 ? (float)total_accessed / total_pages : 0.0f;
    stats_.avg_access_ratio = ratio;
    stats_.current_interval_ms = timer_.get_interval_ms();

    // 动态调整频率
    timer_.auto_adjust_rate(ratio);

    // 每 10 个周期输出一次统计
    if (current_sequence_ % 10 == 0) {
        IDLE_LOGI("Sample #%llu: %zu/%zu accessed (%.1f%%)",
                  (unsigned long long)current_sequence_,
                  total_accessed, total_pages, ratio * 100);
    }
}

// ==================== 日志 ====================

void IdlePageMonitor::write_log_header() {
    const char* header = "# mem_visit.log - Idle Page Tracking\n"
                         "# Format: timestamp_us,sequence,vaddr,pfn,accessed,region_name\n"
                         "# accessed: 1=accessed, 0=idle, -1=unknown (no PFN)\n"
                         "# region_name: heap, base.apk, or memory permissions (r-xp, rw-p)\n";

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

const char* IdlePageMonitor::lookup_permission(uintptr_t vaddr) const {
    for (const auto& region : target_regions_) {
        if (vaddr >= region.start && vaddr < region.end) {
            return region.perms;
        }
    }
    return "----";
}

IdlePageMonitor::Stats IdlePageMonitor::get_stats() const {
    return stats_;
}

// ==================== 堆内存动态跟踪 ====================

void IdlePageMonitor::track_allocation(uintptr_t addr, size_t size, uint32_t flags) {
    // SO 模式下不跟踪堆内存分配
    if (mode_ == MonitorMode::SO_CODE_SECTIONS) {
        return;
    }

    (void)flags;  // 保留用于未来扩展
    if (addr == 0 || size == 0) return;

    // 页对齐处理：向下对齐到 4KB 边界
    uintptr_t page_start = addr & ~0xFFFULL;
    // 向上对齐到 4KB 边界
    uintptr_t page_end = (addr + size + 4095) & ~0xFFFULL;

    // 如果区域已存在，跳过
    if (region_exists(page_start, page_end)) {
        return;
    }

    SampleTask task;
    task.type = TaskType::ADD_REGION;
    task.timestamp_us = get_timestamp_us();
    task.sequence_id = 0;  // ADD_REGION 不需要 sequence_id
    task.region_start = page_start;
    task.region_end = page_end;
    task.region_flags = flags;  // 使用传入的 flags 参数

    // 异步提交任务（非阻塞，队列满时丢弃）
    if (!task_queue_.enqueue(task)) {
        IDLE_LOGD("Task queue full, dropping ADD_REGION for 0x%llx-0x%llx",
                  (unsigned long long)page_start, (unsigned long long)page_end);
    }
}

void IdlePageMonitor::untrack_allocation(uintptr_t addr, size_t size) {
    // TODO: 实现区域移除（需要维护 address->size 映射）
    // 当前简化处理：只添加不删除，避免复杂的状态同步
    (void)addr;
    (void)size;
}

void IdlePageMonitor::handle_add_region(uintptr_t start, uintptr_t end, uint32_t flags) {
    (void)flags;  // 保留用于未来扩展

    // 再次检查（双检锁模式，虽然这里不是锁）
    if (region_exists(start, end)) {
        return;
    }

    MemoryRegion region;
    region.start = start;
    region.end = end;
    strncpy(region.perms, "rw-p", sizeof(region.perms) - 1);
    region.perms[sizeof(region.perms) - 1] = '\0';
    region.offset = 0;
    region.name = "heap";
    region.is_monitor = true;

    target_regions_.push_back(region);

    size_t pages = (end - start) / 4096;
    IDLE_LOGI("[HeapTrack] Added region #%zu: 0x%llx-0x%llx (%zu pages)",
              target_regions_.size(),
              (unsigned long long)start, (unsigned long long)end, pages);
}

bool IdlePageMonitor::region_exists(uintptr_t start, uintptr_t end) const {
    for (const auto& r : target_regions_) {
        // 完全重叠或包含
        if (r.start == start && r.end == end) {
            return true;
        }
        // 部分重叠也视为存在（简化处理）
        if (start < r.end && end > r.start) {
            return true;
        }
    }
    return false;
}

// ==================== C 接口 ====================

bool idle_page_monitor_init(int mode, const char* so_name, const char* log_path) {
    auto monitor_mode = static_cast<idle_page::IdlePageMonitor::MonitorMode>(mode);
    return IdlePageMonitor::instance().init(monitor_mode, so_name, log_path, 100);
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

void idle_page_track_allocation(void* addr, size_t size) {
    IdlePageMonitor::instance().track_allocation(reinterpret_cast<uintptr_t>(addr), size);
}

void idle_page_untrack_allocation(void* addr, size_t size) {
    IdlePageMonitor::instance().untrack_allocation(reinterpret_cast<uintptr_t>(addr), size);
}

} // namespace idle_page
