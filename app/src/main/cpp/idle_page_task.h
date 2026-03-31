//
// idle_page_task.h
// Idle Page 监控任务队列定义
//

#ifndef DEMO_SO_IDLE_PAGE_TASK_H
#define DEMO_SO_IDLE_PAGE_TASK_H

#include <cstdint>
#include <atomic>
#include <cstddef>
#include <mutex>

namespace idle_page {

// 任务类型
enum class TaskType : uint8_t {
    SAMPLE_START = 0,    // 开始新周期：设置所有页为 idle
    SAMPLE_END = 1,      // 结束周期：读取访问状态并输出日志
    SHUTDOWN = 2,        // 停止工作线程
    ADD_REGION = 3       // 动态添加监控区域（堆内存分配）
};

// 采样任务结构
struct SampleTask {
    TaskType type;
    uint64_t timestamp_us;  // 任务创建时间（与 mem_reg.log 同源）
    uint64_t sequence_id;   // 采样周期序号

    // SAMPLE_START: 监控范围限制（可选）
    // ADD_REGION: 新区域的起始和结束地址
    uintptr_t region_start;
    uintptr_t region_end;

    // ADD_REGION 使用：区域标志（如分配类型、热态等级）
    uint32_t region_flags = 0;
};

// 无锁单生产者单消费者队列
class TaskQueue {
public:
    static constexpr size_t CAPACITY = 512;

    TaskQueue();

    // 生产者调用（定时器线程 + 任意调用malloc的线程）- 多生产者，需加锁
    bool enqueue(const SampleTask& task);

    // 消费者调用（工作线程）- 单消费者，无锁
    bool dequeue(SampleTask& task);

    // 非阻塞检查
    bool empty() const;

private:
    mutable std::mutex mutex_;                      // 保护多生产者入队
    alignas(64) std::atomic<size_t> head_{0};  // 生产者写入位置
    alignas(64) std::atomic<size_t> tail_{0};  // 消费者读取位置
    SampleTask buffer_[CAPACITY];
};

} // namespace idle_page

#endif // DEMO_SO_IDLE_PAGE_TASK_H