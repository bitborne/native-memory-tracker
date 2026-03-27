//
// Created by Schatten on 2026/3/12.
//

#include "log_hooks.h"
#include "log_buffer.h"
#include "idle_page_monitor.h"
#include <android/log.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cstdarg>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <sys/mman.h>
#include <malloc.h>
#include <unwind.h>

// 辅助函数: 时间戳获取（微秒级）
static uint64_t get_timestamp_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
}

static _Unwind_Reason_Code unwind_callback(struct _Unwind_Context* context, void* arg) {
    BacktraceState* state = static_cast<BacktraceState*>(arg);
    uintptr_t pc = _Unwind_GetIP(context);
    if (pc) {
        if (state->current == state->end) {
            return _URC_END_OF_STACK;
        } else {
            *state->current++ = reinterpret_cast<void*>(pc);
        }
    }
    return _URC_NO_REASON;
}

static void get_backtrace(void** buffer, int max_depth) {
    BacktraceState state = {buffer, buffer + max_depth};
    _Unwind_Backtrace(unwind_callback, &state);
}


// 调试计数器
//static std::atomic<uint64_t> hook_count{0};

// 1. Malloc Hook
void* my_malloc(size_t size) {
    BYTEHOOK_STACK_SCOPE();
    
//    // 每 100 次打印一次调试信息到 logcat
//    uint64_t cnt = hook_count.fetch_add(1);
//    if (cnt % 1000 == 0) {
//        LOGI("Hook triggered: malloc(%zu), total_calls=%llu", size, (unsigned long long)cnt);
//    }

    void* result = BYTEHOOK_CALL_PREV(my_malloc, size);

    // 获取调用栈（最多5层）
    void* backtrace[5] = {0};
    get_backtrace(backtrace, 5);

    // 格式：时间戳,类型,地址,请求大小,实际大小,tid,0,0,栈1,栈2,栈3...
    // TODO: 中间两个 0 作用未知, 记得问
    fast_write_log("%lu,%d,%p,%zu,%zu,%d,0,0,%p,%p,%p,%p,%p",
              get_timestamp_us(), TYPE_MALLOC, result, size,
              malloc_usable_size(result), gettid(),
              backtrace[0], backtrace[1], backtrace[2], backtrace[3], backtrace[4]);

    // 添加到 IdlePageMonitor 进行访问跟踪
    if (result) {
        idle_page::IdlePageMonitor::instance().track_allocation(
            reinterpret_cast<uintptr_t>(result), malloc_usable_size(result));
    }

    return result;
}

// 2. Free Hook
void my_free(void* ptr) {
    BYTEHOOK_STACK_SCOPE();

    // Free 不需要大小，大小填0（根据图片示例）
    void* backtrace[5] = {0};
    get_backtrace(backtrace, 5);

    fast_write_log("%lu,%d,%p,0,0,0,0,0",
              get_timestamp_us(), TYPE_FREE, ptr);

    BYTEHOOK_CALL_PREV(my_free, ptr);
}

// 3. Calloc Hook
void* my_calloc(size_t num, size_t size) {
    BYTEHOOK_STACK_SCOPE();

    void* result = BYTEHOOK_CALL_PREV(my_calloc, num, size);

    void* backtrace[5] = {0};
    get_backtrace(backtrace, 5);

    fast_write_log("%lu,%d,%p,%zu,%zu,%d,0,0,%p,%p,%p,%p,%p",
              get_timestamp_us(), TYPE_CALLOC, result, num * size,
              malloc_usable_size(result), gettid(),
              backtrace[0], backtrace[1], backtrace[2], backtrace[3], backtrace[4]);

    // 添加到 IdlePageMonitor 进行访问跟踪
    if (result) {
        idle_page::IdlePageMonitor::instance().track_allocation(
            reinterpret_cast<uintptr_t>(result), malloc_usable_size(result));
    }

    return result;
}

// 4. Realloc Hook
void* my_realloc(void* ptr, size_t size) {
    BYTEHOOK_STACK_SCOPE();

    void* result = BYTEHOOK_CALL_PREV(my_realloc, ptr, size);

    void* backtrace[5] = {0};
    get_backtrace(backtrace, 5);

    fast_write_log("%lu,%d,%p,%zu,%zu,%d,0,0,%p,%p,%p,%p,%p",
              get_timestamp_us(), TYPE_REALLOC, result, size,
              malloc_usable_size(result), gettid(),
              backtrace[0], backtrace[1], backtrace[2], backtrace[3], backtrace[4]);

    // 添加到 IdlePageMonitor 进行访问跟踪
    if (result) {
        idle_page::IdlePageMonitor::instance().track_allocation(
            reinterpret_cast<uintptr_t>(result), malloc_usable_size(result));
    }

    return result;
}

// 5. Mmap Hook
void* my_mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) {
    BYTEHOOK_STACK_SCOPE();

    void* result = BYTEHOOK_CALL_PREV(my_mmap, addr, length, prot, flags, fd, offset);

    void* backtrace[5] = {0};
    get_backtrace(backtrace, 5);

    fast_write_log("%lu,%d,%p,%zu,%zu,%d,0,0,%p,%p,%p,%p,%p",
              get_timestamp_us(), TYPE_MMAP, result, length,
              length, gettid(),  // mmap实际大小就是length
              backtrace[0], backtrace[1], backtrace[2], backtrace[3], backtrace[4]);

    // 添加到 IdlePageMonitor 进行访问跟踪
    if (result != MAP_FAILED) {
        idle_page::IdlePageMonitor::instance().track_allocation(
            reinterpret_cast<uintptr_t>(result), length);
    }

    return result;
}

// 6. Munmap Hook
int my_munmap(void* addr, size_t length) {
    BYTEHOOK_STACK_SCOPE();

// 简化, 无调用栈
//    void* backtrace[5] = {0};
//    get_backtrace(backtrace, 5);
    fast_write_log("%lu,%d,%p,%zu,0,0,0,0,0", // 5 个 0 后, 不再有调用栈地址信息
              get_timestamp_us(), TYPE_MUNMAP, addr, length);

    return BYTEHOOK_CALL_PREV(my_munmap, addr, length);
}

// 7. mmap64 hook（处理大文件映射）
void* my_mmap64(void* addr, size_t length, int prot, int flags, int fd, off64_t offset) {
    BYTEHOOK_STACK_SCOPE();
    void* result = BYTEHOOK_CALL_PREV(my_mmap64, addr, length, prot, flags, fd, offset);

    void* backtrace[5] = {0};
    get_backtrace(backtrace, 5);

    fast_write_log("%lu,%d,%p,%zu,%zu,%d,0,0,%p,%p,%p,%p,%p",
              get_timestamp_us(), TYPE_MMAP64, result,
              length, length, gettid(),
              backtrace[0], backtrace[1], backtrace[2], backtrace[3], backtrace[4]);

    // 添加到 IdlePageMonitor 进行访问跟踪
    if (result != MAP_FAILED) {
        idle_page::IdlePageMonitor::instance().track_allocation(
            reinterpret_cast<uintptr_t>(result), length);
    }

    return result;
}

// 8. Hook posix_memalign（游戏/图形常用）
int my_posix_memalign(void** memptr, size_t alignment, size_t size) {
    BYTEHOOK_STACK_SCOPE();

    int result = BYTEHOOK_CALL_PREV(my_posix_memalign, memptr, alignment, size);

    // 获取调用栈（最多5层）
    void* backtrace[5] = {0};
    get_backtrace(backtrace, 5);

    // 格式：时间戳,类型,地址,请求大小,实际大小,tid,0,0,栈1,栈2,栈3...

    fast_write_log("%lu,%d,%p,%zu,%zu,%d,0,0,%p,%p,%p,%p,%p",
              get_timestamp_us(), TYPE_POSIX_MEMALIGN, *memptr,
              size, malloc_usable_size(*memptr), gettid(),
              backtrace[0], backtrace[1], backtrace[2], backtrace[3], backtrace[4]);

    // 添加到 IdlePageMonitor 进行访问跟踪
    if (result == 0 && *memptr) {
        idle_page::IdlePageMonitor::instance().track_allocation(
            reinterpret_cast<uintptr_t>(*memptr), malloc_usable_size(*memptr));
    }

    return result;
}

// 9. Hook aligned_alloc
void* my_aligned_alloc(size_t alignment, size_t size) {
    BYTEHOOK_STACK_SCOPE();

    void* result = BYTEHOOK_CALL_PREV(my_aligned_alloc, alignment, size);

    // 获取调用栈（最多5层）
    void* backtrace[5] = {0};
    get_backtrace(backtrace, 5);

    // 格式：时间戳,类型,地址,请求大小,实际大小,tid,0,0,栈1,栈2,栈3...
    fast_write_log("%lu,%d,%p,%zu,%zu,%d,0,0,%p,%p,%p,%p,%p",
              get_timestamp_us(), TYPE_ALIGNED_ALLOC, result,
              size, malloc_usable_size(result), gettid(),
              backtrace[0], backtrace[1], backtrace[2], backtrace[3], backtrace[4]);

    // 添加到 IdlePageMonitor 进行访问跟踪
    if (result) {
        idle_page::IdlePageMonitor::instance().track_allocation(
            reinterpret_cast<uintptr_t>(result), malloc_usable_size(result));
    }

    return result;
}
