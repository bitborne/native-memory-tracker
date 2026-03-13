//
// Created by Schatten on 2026/3/12.
//

#ifndef DEMO_SO_LOG_HOOKS_H
#define DEMO_SO_LOG_HOOKS_H

#include <cstddef>
#include <sys/types.h>
#include "bytehook.h"

#include <android/log.h>
#define LOG_TAG "SO2_Hook"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

enum AllocType {
    TYPE_MALLOC = 1,
    TYPE_REALLOC = 2,
    TYPE_CALLOC = 3,
    TYPE_FREE = 4,
    TYPE_MMAP = 5,
    TYPE_MUNMAP = 6,
    TYPE_MMAP64 = 7,          // 新增
    TYPE_POSIX_MEMALIGN = 8,  // 新增
    TYPE_ALIGNED_ALLOC = 9     // 新增
};

// 获取调用栈（简化版，取多层返回地址）
struct BacktraceState {
    void** current;
    void** end;
};

// Proxy 函数声明
void* my_malloc(size_t size);
void my_free(void* ptr);
void* my_calloc(size_t num, size_t size);
void* my_realloc(void* ptr, size_t size);
void* my_mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset);
int my_munmap(void* addr, size_t length);
void* my_mmap64(void* addr, size_t length, int prot, int flags, int fd, off64_t offset);
int my_posix_memalign(void** memptr, size_t alignment, size_t size);
void* my_aligned_alloc(size_t alignment, size_t size);



#endif //DEMO_SO_LOG_HOOKS_H
