//
// Created by Schatten on 2026/3/12.
//

#ifndef DEMO_SO_LOG_BUFFER_H
#define DEMO_SO_LOG_BUFFER_H

#include <atomic>
#include <chrono>
#include <cstring>
#include <sys/mman.h>
#include <thread>
// ==================== 配置参数 ====================
constexpr size_t RING_BUFFER_SIZE = 16 * 1024 * 1024;  // 16MB 环形缓冲区
constexpr size_t MAX_RECORD_SIZE = 1024;               // 单条记录最大长度（必须 > BATCH_SIZE）
constexpr size_t BATCH_SIZE = 512;                     // 512B 线程缓冲区（必须 < MAX_RECORD_SIZE）
constexpr uint32_t SAMPLE_RATE = 1;                    // 采样率 1/1 (全记录)，可改为 10 (1/10采样)
// ==================== 无锁环形队列 ====================
class LockFreeRingBuffer {
private:
    struct Record;

    Record* buffer;
    size_t mask;
    std::atomic<uint64_t> write_idx{0};
    std::atomic<uint64_t> read_idx{0};

public:
    explicit LockFreeRingBuffer(size_t size);

    ~LockFreeRingBuffer();

    // 生产者：尝试写入
    bool try_enqueue(const char* data, size_t len);

    // 消费者：批量读取
    size_t try_dequeue_batch(char* out_buffer, size_t batch_count);

    bool is_empty() const;
};

// ==================== 全局日志管理器 ====================
class LogManager {
private:
    LockFreeRingBuffer ring_buffer{RING_BUFFER_SIZE / MAX_RECORD_SIZE};
    std::thread writer_thread;
    std::atomic<bool> running{true};
    int fd = -1;

    // 后台写入循环
    void writer_loop();

public:
    bool init(const char* path);

    void shutdown();

    // 提交到全局队列（TLS 缓冲区满时调用）
    bool submit_to_global(const char* data, size_t len);

    // 直接写入文件（用于元数据，如SO基址）
    bool write_raw(const char* data, size_t len);

    static LogManager& instance();
};

// ==================== 高性能日志写入 ====================
void fast_write_log(const char* fmt, ...);

// 强制刷新当前线程的 TLS 缓冲区到文件
void flush_tls_buffer();

#endif //DEMO_SO_LOG_BUFFER_H
