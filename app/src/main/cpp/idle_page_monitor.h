//
// idle_page_monitor.h
// Idle Page 监控主类 - 整合所有组件
//

#ifndef DEMO_SO_IDLE_PAGE_MONITOR_H
#define DEMO_SO_IDLE_PAGE_MONITOR_H

#include "idle_page_task.h"
#include "idle_page_mmap.h"
#include "idle_page_elf.h"
#include "idle_page_timer.h"

#include <cstdint>
#include <vector>
#include <thread>
#include <atomic>
#include <string>

namespace idle_page {

// IdlePageMonitor: 主监控类（单例）
class IdlePageMonitor {
public:
    static IdlePageMonitor& instance();

    // 禁止拷贝
    IdlePageMonitor(const IdlePageMonitor&) = delete;
    IdlePageMonitor& operator=(const IdlePageMonitor&) = delete;

    // 初始化监控
    // so_name: 要监控的 SO 文件名，如 "libdemo_so.so"
    // log_path: 日志文件路径（mem_visit.log）
    // initial_interval_ms: 初始采样周期
    bool init(const char* so_name, const char* log_path, int initial_interval_ms = 100);

    // 设置监控目标范围（从 maps 自动检测，也可手动指定）
    void set_target_range(uintptr_t start, uintptr_t end);

    // 启停监控
    void start();
    void stop();

    // 完全关闭（释放资源）
    void shutdown();

    // 是否正在运行
    bool is_running() const { return running_; }

    // 获取统计信息
    struct Stats {
        uint64_t total_samples;
        uint64_t total_pages;
        uint64_t accessed_pages;
        float avg_access_ratio;
        int current_interval_ms;
    };
    Stats get_stats() const;

private:
    IdlePageMonitor() = default;
    ~IdlePageMonitor();

    // 工作线程主循环
    void worker_loop();

    // 任务执行
    void execute_task(const SampleTask& task);

    // 采样操作
    void do_sample_start(uintptr_t start, uintptr_t end);
    void do_sample_end();

    // 初始化页缓存（首次采样时调用）
    bool init_page_cache(uintptr_t start, uintptr_t end);

    // 日志写入
    void write_log_header();
    void flush_log_buffer();

    // 获取时间戳（微秒，与 mem_reg.log 同源）
    static uint64_t get_timestamp_us();

    // ===== 组件 =====
    MmapPagemap pagemap_;
    MmapPageIdle page_idle_;
    RuntimeSectionResolver section_resolver_;
    IdlePageTimer timer_;
    TaskQueue task_queue_;

    // ===== 配置 =====
    std::string so_name_;
    std::string log_path_;
    uintptr_t target_start_ = 0;
    uintptr_t target_end_ = 0;

    // ===== 状态 =====
    std::atomic<bool> running_{false};
    std::atomic<bool> worker_running_{false};
    std::atomic<bool> first_sample_{true};
    std::thread worker_thread_;

    // ===== 采样数据 =====
    std::vector<PageInfo> page_cache_;      // PFN 缓存
    std::vector<uint64_t> pfn_list_;        // PFN 列表（批量操作）
    std::vector<bool> access_results_;      // 访问结果

    // ===== 日志 =====
    int log_fd_ = -1;
    static constexpr size_t LOG_BUFFER_SIZE = 64 * 1024;
    char log_buffer_[LOG_BUFFER_SIZE];
    size_t log_offset_ = 0;

    // ===== 统计 =====
    std::atomic<uint64_t> sequence_id_{0};
    uint64_t current_sequence_ = 0;
    Stats stats_;
};

// C 接口（供 JNI 调用）
extern "C" {
    bool idle_page_monitor_init(const char* so_name, const char* log_path);
    void idle_page_monitor_start();
    void idle_page_monitor_stop();
    void idle_page_monitor_shutdown();
}

} // namespace idle_page

#endif // DEMO_SO_IDLE_PAGE_MONITOR_H
