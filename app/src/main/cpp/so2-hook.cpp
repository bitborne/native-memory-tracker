#include "log_buffer.h"
#include "log_hooks.h"
#include "idle_page_monitor.h"

#include <jni.h>
#include <fcntl.h>
#include <unistd.h>     // for gettid()
#include <cstring>
#include <cstdarg>
#include <chrono>
#include <dlfcn.h>
#include <link.h>       // for dl_iterate_phdr
#include <unwind.h>
#include <malloc.h>    // for malloc_usable_size()

// 日志文件 FD
static int g_log_fd = -1;

// SO加载基址（用于符号解析）
static uintptr_t g_so_base_addr = 0;

// 回调函数：用于dl_iterate_phdr查找SO基址
static int find_so_base_callback(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size;
    const char* target_so = static_cast<const char*>(data);

    if (info->dlpi_name && strstr(info->dlpi_name, target_so)) {
        g_so_base_addr = info->dlpi_addr;
        LOGI("Found %s base address: 0x%llx", info->dlpi_name, (unsigned long long)g_so_base_addr);
        return 1; // 找到后停止遍历
    }
    return 0; // 继续遍历
}

// 获取并记录SO基址到日志文件
static void log_so_base_address(const char* so_name) {
    // 遍历所有加载的共享库
    dl_iterate_phdr(find_so_base_callback, (void*)so_name);

    // 将基址写入日志文件头部作为注释
    if (g_so_base_addr != 0) {
        char header[256];
        snprintf(header, sizeof(header),
                 "# SO_BASE_ADDRESS: %s=0x%llx\n",
                 so_name, (unsigned long long)g_so_base_addr);
        LogManager::instance().write_raw(header, strlen(header));

        // 同时写入一个结构化的记录（时间戳=0表示元数据）
        char meta[512];
        snprintf(meta, sizeof(meta),
                 "0,0,0x%llx,0,0,0,0,0,SO_BASE,%s\n",
                 (unsigned long long)g_so_base_addr, so_name);
        LogManager::instance().write_raw(meta, strlen(meta));

        LOGI("SO base address logged: 0x%llx", (unsigned long long)g_so_base_addr);
    } else {
        LOGI("SO base address not found for: %s", so_name);
    }
}

// Hook stubs，用于 unhook
static bytehook_stub_t g_stub_malloc = nullptr;
static bytehook_stub_t g_stub_free = nullptr;
static bytehook_stub_t g_stub_calloc = nullptr;
static bytehook_stub_t g_stub_realloc = nullptr;
static bytehook_stub_t g_stub_mmap = nullptr;
static bytehook_stub_t g_stub_munmap = nullptr;
static bytehook_stub_t g_stub_mmap64 = nullptr;
static bytehook_stub_t g_stub_posix_memalign = nullptr;
static bytehook_stub_t g_stub_aligned_alloc = nullptr;

static bool g_hooked = false;

// 初始化 Hook
extern "C" JNIEXPORT void JNICALL
Java_com_example_demo_1so_MainActivity_nativeInitHook(JNIEnv* env, jobject thiz, jstring log_path) {
    const char* path = env->GetStringUTFChars(log_path, nullptr);

    LOGI("=================================");
    LOGI("SO2: Opening log file: %s", path);

    // 初始化 LogManager（用于 fast_write_log）
    if (!LogManager::instance().init(path)) {
        LOGE("Failed to init LogManager: %s", path);
        env->ReleaseStringUTFChars(log_path, path);
        return;
    }
    LOGI("SO2: LogManager initialized");

    // 打开日志文件（追加模式）- 保留用于 write_log
    // 注意：不使用 O_TRUNC，因为 LogManager::init 已经清空了文件
    g_log_fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (g_log_fd < 0) {
        LOGE("Failed to open log file: %s! errno = %d", path, errno);
        env->ReleaseStringUTFChars(log_path, path);
        return;
    }

    LOGI("SO2: Log file opened, fd=%d", g_log_fd);

    // 初始化 ByteHook（自动模式 + 开启调试日志）
    int ret = bytehook_init(BYTEHOOK_MODE_AUTOMATIC, true);
    LOGI("ByteHook init result: %d", ret);

    // 只 hook libdemo_so.so（so1）
    const char* target_so = "libdemo_so.so";

    // Hook 所有内存分配函数，保存 stub 用于 unhook
    g_stub_malloc = bytehook_hook_single(target_so, nullptr, "malloc", (void*)my_malloc, nullptr, nullptr);
    g_stub_free = bytehook_hook_single(target_so, nullptr, "free", (void*)my_free, nullptr, nullptr);
    g_stub_calloc = bytehook_hook_single(target_so, nullptr, "calloc", (void*)my_calloc, nullptr, nullptr);
    g_stub_realloc = bytehook_hook_single(target_so, nullptr, "realloc", (void*)my_realloc, nullptr, nullptr);
    g_stub_mmap = bytehook_hook_single(target_so, nullptr, "mmap", (void*)my_mmap, nullptr, nullptr);
    g_stub_munmap = bytehook_hook_single(target_so, nullptr, "munmap", (void*)my_munmap, nullptr, nullptr);
    g_stub_mmap64 = bytehook_hook_single(target_so, nullptr, "mmap64", (void*)my_mmap64, nullptr, nullptr);
    g_stub_posix_memalign = bytehook_hook_single(target_so, nullptr, "posix_memalign", (void*)my_posix_memalign, nullptr, nullptr);
    g_stub_aligned_alloc = bytehook_hook_single(target_so, nullptr, "aligned_alloc", (void*)my_aligned_alloc, nullptr, nullptr);

    LOGI("SO2 Hook initialized, logging to: %s", path);
    g_hooked = true;

    // 记录SO加载基址（用于后续符号解析）
    log_so_base_address(target_so);

    // 初始化 Idle Page Monitor（内存访问监控）
    // mem_visit.log 路径与 mem_reg.log 同目录
    char visit_log_path[512];
    const char* last_slash = strrchr(path, '/');
    if (last_slash) {
        int dir_len = last_slash - path + 1;
        snprintf(visit_log_path, sizeof(visit_log_path), "%.*s%s",
                 dir_len, path, "mem_visit.log");
    } else {
        snprintf(visit_log_path, sizeof(visit_log_path), "mem_visit.log");
    }


    // 100ms 初始周期，自动调整
    // 模式选择：
    //   0 = SO_CODE_SECTIONS (监控SO代码段，日志显示权限+文件名)
    //   1 = HEAP_ALLOCATIONS (监控堆内存，日志显示(heap))
    auto mode = idle_page::IdlePageMonitor::MonitorMode::HEAP_ALLOCATIONS;
    if (idle_page::IdlePageMonitor::instance().init(mode, "libdemo_so.so", visit_log_path, 100)) {
        LOGI("IdlePageMonitor initialized: %s", visit_log_path);
    } else {
        LOGE("IdlePageMonitor init failed (may need root)");
    }

    // 释放 JNI 字符串
    env->ReleaseStringUTFChars(log_path, path);
}

// 关闭日志（可选）
extern "C" JNIEXPORT void JNICALL
Java_com_example_demo_1so_MainActivity_nativeCloseLog(JNIEnv* env, jobject thiz) {
    if (!g_hooked) {
        return;
    }
    
    // 先 flush 当前线程的 TLS 缓冲区
    flush_tls_buffer();
    
    // 给其他线程一点时间 flush 它们的 TLS 缓冲区到全局队列
    usleep(200000);  // 200ms
    
    // Unhook 所有函数
    if (g_stub_malloc) bytehook_unhook(g_stub_malloc);
    if (g_stub_free) bytehook_unhook(g_stub_free);
    if (g_stub_calloc) bytehook_unhook(g_stub_calloc);
    if (g_stub_realloc) bytehook_unhook(g_stub_realloc);
    if (g_stub_mmap) bytehook_unhook(g_stub_mmap);
    if (g_stub_munmap) bytehook_unhook(g_stub_munmap);
    if (g_stub_mmap64) bytehook_unhook(g_stub_mmap64);
    if (g_stub_posix_memalign) bytehook_unhook(g_stub_posix_memalign);
    if (g_stub_aligned_alloc) bytehook_unhook(g_stub_aligned_alloc);
    
    g_stub_malloc = g_stub_free = g_stub_calloc = g_stub_realloc = nullptr;
    g_stub_mmap = g_stub_munmap = g_stub_mmap64 = nullptr;
    g_stub_posix_memalign = g_stub_aligned_alloc = nullptr;
    
    // 关闭 LogManager（刷新全局队列并关闭文件）
    LogManager::instance().shutdown();
    
    if (g_log_fd >= 0) {
        close(g_log_fd);
        g_log_fd = -1;
    }
    
    // 关闭 Idle Page Monitor
    idle_page::IdlePageMonitor::instance().shutdown();

    g_hooked = false;
    LOGI("Hook closed and unhooked");
}

// ==================== Idle Page Monitor 控制 ====================

extern "C" JNIEXPORT void JNICALL
Java_com_example_demo_1so_MainActivity_nativeStartIdleMonitor(JNIEnv* env, jobject thiz) {
    LOGI("Starting Idle Page Monitor...");
    idle_page::IdlePageMonitor::instance().start();
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_demo_1so_MainActivity_nativeStopIdleMonitor(JNIEnv* env, jobject thiz) {
    LOGI("Stopping Idle Page Monitor...");
    idle_page::IdlePageMonitor::instance().stop();
}