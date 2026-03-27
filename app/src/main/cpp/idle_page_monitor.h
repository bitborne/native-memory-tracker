//
// idle_page_monitor.h
// Idle Page 监控主类 - 整合所有组件
//

#ifndef DEMO_SO_IDLE_PAGE_MONITOR_H
#define DEMO_SO_IDLE_PAGE_MONITOR_H

#include "idle_page_task.h"
#include "idle_page_mmap.h"
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
    // 监控模式
    enum class MonitorMode : uint8_t {
        SO_CODE_SECTIONS = 0,   // 监控SO代码段，日志显示(权限+文件名)
        HEAP_ALLOCATIONS = 1    // 监控堆内存，日志显示(heap)
    };

    static IdlePageMonitor& instance();

    // 禁止拷贝
    IdlePageMonitor(const IdlePageMonitor&) = delete;
    IdlePageMonitor& operator=(const IdlePageMonitor&) = delete;

    // 初始化监控
    // mode: 监控模式（SO代码段 或 堆内存）
    // so_name: SO文件名（仅在SO模式下使用，如 "libdemo_so.so"）
    // log_path: 日志文件路径（mem_visit.log）
    // initial_interval_ms: 初始采样周期
    bool init(MonitorMode mode, const char* so_name, const char* log_path, int initial_interval_ms = 100);

    // 添加监控目标区域
    void add_target_region(const MemoryRegion& region);

    // 设置监控目标（自动从 maps 加载所有区域）
    void set_target_regions(const std::vector<MemoryRegion>& regions);

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

    // ========== 堆内存动态跟踪（供 Hook 调用） ==========

    // 线程安全：异步提交 ADD_REGION 任务
    void track_allocation(uintptr_t addr, size_t size, uint32_t flags = 0);

    // 动态移除区域（对应 free/munmap，暂未实现）
    void untrack_allocation(uintptr_t addr, size_t size);

private:
    IdlePageMonitor() = default;
    ~IdlePageMonitor();

    // 工作线程主循环
    void worker_loop();

    // 任务执行
    void execute_task(const SampleTask& task);

    // 处理 ADD_REGION 任务
    void handle_add_region(uintptr_t start, uintptr_t end, uint32_t flags);

    // 区域去重检查
    bool region_exists(uintptr_t start, uintptr_t end) const;

    // 采样操作 - 分别处理每个区域
    void do_sample_start_all();
    void do_sample_end_all();

    // 为单个区域初始化页缓存
    bool init_page_cache_for_region(const MemoryRegion& region);

    // 日志写入
    void write_log_header();
    void flush_log_buffer();

    // 获取时间戳（微秒，与 mem_reg.log 同源）
    static uint64_t get_timestamp_us();

    // 根据地址查找权限（如 r-xp, rw-p）
    const char* lookup_permission(uintptr_t vaddr) const;

    // ===== 组件 =====
    MmapPagemap pagemap_;
    MmapPageIdle page_idle_;
    IdlePageTimer timer_;
    TaskQueue task_queue_;

    // ===== 运行时区域缓存 =====
    std::vector<MemoryRegion> target_regions_;

    // ===== 配置 =====
    MonitorMode mode_;
    std::string so_name_;
    std::string log_path_;

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

// C 接口（供 JNI/Hook 调用）
extern "C" {
    bool idle_page_monitor_init(int mode, const char* so_name, const char* log_path);
    void idle_page_monitor_start();
    void idle_page_monitor_stop();
    void idle_page_monitor_shutdown();

    // 堆内存跟踪接口（供 log_hooks.cpp 调用）
    void idle_page_track_allocation(void* addr, size_t size);
    void idle_page_untrack_allocation(void* addr, size_t size);
}

} // namespace idle_page

#endif // DEMO_SO_IDLE_PAGE_MONITOR_H
