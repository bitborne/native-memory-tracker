//
// Created by Schatten on 2026/3/12.
//

#include "log_buffer.h"
#include <android/log.h>
#include <fcntl.h>
#include <unistd.h>

struct  LockFreeRingBuffer::Record {
    std::atomic<uint32_t> seq;      // 序列号（用于同步）
    char data[MAX_RECORD_SIZE];     // 日志数据
    uint16_t len;                   // 实际长度
    std::atomic<bool> ready;        // 是否写入完成
};

// 构造函数
LockFreeRingBuffer::LockFreeRingBuffer(size_t size) {
    // 确保 size 是 2 的幂
    size_t power2 = 1;
    while (power2 < size) power2 <<= 1;
    mask = power2 - 1;

    // 使用 mmap 分配大页内存，减少缺页中断
    buffer = (Record*)mmap(nullptr, power2 * sizeof(Record),
                           PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    memset(buffer, 0, power2 * sizeof(Record));
}

LockFreeRingBuffer::~LockFreeRingBuffer() {
    munmap(buffer, (mask + 1) * sizeof(Record));
}

bool LockFreeRingBuffer::try_enqueue(const char* data, size_t len) {
    if (len >= MAX_RECORD_SIZE) return false;

    uint64_t idx = write_idx.fetch_add(1, std::memory_order_relaxed);
    Record& rec = buffer[idx & mask];

    // 检查是否被消费者追上（缓冲区满）
    if (rec.ready.load(std::memory_order_acquire)) {
        // 缓冲区满，丢弃或等待（这里选择丢弃）
        write_idx.fetch_sub(1, std::memory_order_relaxed);
        return false;
    }

    // 复制数据
    memcpy(rec.data, data, len);
    rec.len = len;
    rec.seq.store((uint32_t)idx, std::memory_order_release);
    rec.ready.store(true, std::memory_order_release);

    return true;
}

size_t LockFreeRingBuffer::try_dequeue_batch(char *out_buffer, size_t batch_count) {
    size_t total = 0;
    size_t count = 0;

    while (count < batch_count) {
        Record& rec = buffer[read_idx & mask];

        if (!rec.ready.load(std::memory_order_acquire)) {
            break;  // 没有数据了
        }

        // 复制到输出缓冲区
        memcpy(out_buffer + total, rec.data, rec.len);
        total += rec.len;
        out_buffer[total++] = '\n';  // 添加换行

        // 标记为可复用
        rec.ready.store(false, std::memory_order_release);
        read_idx.fetch_add(1, std::memory_order_relaxed);
        count++;
    }

    return total;
}

bool LockFreeRingBuffer::is_empty() const {

    return write_idx.load(std::memory_order_relaxed) ==
           read_idx.load(std::memory_order_relaxed);

}

// ==================== 全局日志管理器 ====================

void LogManager::writer_loop() {
    // 64 条记录 * (MAX_RECORD_SIZE + 1 换行) = 约 64KB
    char batch_buffer[64 * (MAX_RECORD_SIZE + 1)];

    while (running.load()) {
        // 尝试批量读取（最多 64 条）
        size_t n = ring_buffer.try_dequeue_batch(batch_buffer, 64);

        if (n > 0) {
            // 批量写入文件
            write(fd, batch_buffer, n);

            // 每 16KB 强制刷盘一次（平衡性能和可靠性）
            static size_t written_since_fsync = 0;
            written_since_fsync += n;
            if (written_since_fsync >= 16 * 1024) {
                fdatasync(fd);  // 比 fsync 更快，只刷数据不刷元数据
                written_since_fsync = 0;
            }
        } else {
            // 没有数据，短暂休眠（避免 CPU 空转）
            usleep(1000);  // 1ms
        }
    }

    // 刷新剩余数据（使用相同大小的缓冲区）
    while (true) {
        size_t n = ring_buffer.try_dequeue_batch(batch_buffer, 64);
        if (n == 0) break;
        write(fd, batch_buffer, n);
    }
    fsync(fd);
}

bool LogManager::init(const char *path) {
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return false;

    // 使用直接 I/O 可选（绕过页缓存，适合大日志）
    // fcntl(fd, F_SETFL, O_DIRECT);

    writer_thread = std::thread(&LogManager::writer_loop, this);
    return true;
}

// ==================== 线程本地缓冲区（TLS）=======================
thread_local struct {
    char buffer[BATCH_SIZE];  // 512B 线程本地缓冲
    size_t offset = 0;        // 必须初始化！
    uint64_t count = 0;       // 计数器，用于采样
    uint64_t lines = 0;       // 记录条数，用于定期 flush
} tls_log;

// 定期 flush 阈值：每 16 条日志 flush 一次（避免数据积压）
constexpr uint32_t FLUSH_INTERVAL = 16;

// flush 当前线程的 TLS 缓冲区到全局队列
void flush_tls_buffer() {
    if (tls_log.offset > 0) {
        LogManager::instance().submit_to_global(tls_log.buffer, tls_log.offset);
        tls_log.offset = 0;
    }
}

void LogManager::shutdown() {
    // 先 flush TLS 缓冲区
    flush_tls_buffer();
    
    running.store(false);
    if (writer_thread.joinable()) {
        writer_thread.join();
    }
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

bool LogManager::submit_to_global(const char *data, size_t len) {
    return ring_buffer.try_enqueue(data, len);
}

bool LogManager::write_raw(const char *data, size_t len) {
    if (fd < 0) return false;
    ssize_t written = write(fd, data, len);
    return written == (ssize_t)len;
}

LogManager& LogManager::instance() {
    static LogManager inst;
    return inst;
}

// 调试计数器
static std::atomic<uint64_t> log_submit_success{0};
static std::atomic<uint64_t> log_submit_fail{0};

// ==================== 高性能日志写入 ====================
void fast_write_log(const char* fmt, ...) {
    // 采样检查（如果 SAMPLE_RATE > 1）
    if constexpr (SAMPLE_RATE > 1) {
        if ((++tls_log.count % SAMPLE_RATE) != 0) return;
    }

    // 格式化到临时缓冲区（栈分配，避免堆分配）
    char temp[MAX_RECORD_SIZE];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(temp, sizeof(temp), fmt, args);
    va_end(args);

    if (len <= 0 || len >= MAX_RECORD_SIZE) {
//        __android_log_print(ANDROID_LOG_ERROR, "SO2_DEBUG", "fast_write_log: invalid len=%d", len);
        return;
    }

    // 检查 TLS 缓冲区是否足够
    if (tls_log.offset + len + 1 > BATCH_SIZE) {
        // 刷新到全局队列
        bool ok = LogManager::instance().submit_to_global(tls_log.buffer, tls_log.offset);
        if (ok) {
            log_submit_success++;
        } else {
            log_submit_fail++;
//            __android_log_print(ANDROID_LOG_ERROR, "SO2_DEBUG", "submit_to_global FAILED! success=%llu fail=%llu",
//                (unsigned long long)log_submit_success.load(), (unsigned long long)log_submit_fail.load());
        }
        tls_log.offset = 0;
        tls_log.lines = 0;
    }

    // 写入 TLS 缓冲区（无锁，线程安全）
    memcpy(tls_log.buffer + tls_log.offset, temp, len);
    tls_log.offset += len;
    tls_log.buffer[tls_log.offset++] = '\n';
    tls_log.lines++;
    
    // 定期自动 flush：每 FLUSH_INTERVAL 条日志刷新一次
    // 这是关键！防止数据卡在 TLS 缓冲区不写入文件
    if (tls_log.lines >= FLUSH_INTERVAL) {
        bool ok = LogManager::instance().submit_to_global(tls_log.buffer, tls_log.offset);
        if (ok) {
            log_submit_success++;
//            __android_log_print(ANDROID_LOG_INFO, "SO2_DEBUG", "flush OK, offset=%zu, total_success=%llu",
//                tls_log.offset, (unsigned long long)log_submit_success.load());
        } else {
            log_submit_fail++;
//            __android_log_print(ANDROID_LOG_ERROR, "SO2_DEBUG", "flush FAILED! offset=%zu, fail=%llu",
//                tls_log.offset, (unsigned long long)log_submit_fail.load());
        }
        tls_log.offset = 0;
        tls_log.lines = 0;
    }
}
