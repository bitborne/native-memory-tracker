//
// idle_page_task.cpp
// 无锁任务队列实现
//

#include "idle_page_task.h"

namespace idle_page {

TaskQueue::TaskQueue() = default;

bool TaskQueue::enqueue(const SampleTask& task) {
    std::lock_guard<std::mutex> lock(mutex_);

    const size_t current_head = head_.load(std::memory_order_relaxed);
    const size_t next_head = (current_head + 1) % CAPACITY;

    // 检查队列是否已满
    if (next_head == tail_.load(std::memory_order_acquire)) {
        return false;  // 队列满
    }

    buffer_[current_head] = task;
    head_.store(next_head, std::memory_order_release);
    return true;
}

bool TaskQueue::dequeue(SampleTask& task) {
    const size_t current_tail = tail_.load(std::memory_order_relaxed);

    // 检查队列是否为空
    if (current_tail == head_.load(std::memory_order_acquire)) {
        return false;  // 队列空
    }

    task = buffer_[current_tail];
    tail_.store((current_tail + 1) % CAPACITY, std::memory_order_release);
    return true;
}

bool TaskQueue::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return head_.load(std::memory_order_acquire) ==
           tail_.load(std::memory_order_acquire);
}

} // namespace idle_page
