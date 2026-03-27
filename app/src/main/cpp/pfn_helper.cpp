//
// pfn_helper.cpp
// PFN 查询守护进程 - 需要 root 权限运行
// 通过 Unix Socket 提供 /proc/<pid>/pagemap 查询服务
//
// 使用方法：
//   adb push pfn_helper /data/local/tmp/
//   adb shell su -c "/data/local/tmp/pfn_helper <pid>"
//
// 协议：
//   请求：uint64_t vaddr（虚拟地址）
//   响应：uint64_t pfn（物理页帧号，0 表示失败/无映射）
//

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>

#define SOCKET_PATH "/data/local/tmp/pfn_helper.sock"
#define BUFFER_SIZE 256

// PFN 掩码和标志（与 idle_page_mmap.h 保持一致）
static constexpr uint64_t PFN_MASK = (1ULL << 55) - 1;
static constexpr uint64_t PAGE_PRESENT = (1ULL << 63);

static int g_pagemap_fd = -1;
static volatile bool g_running = true;

void signal_handler(int sig) {
    g_running = false;
}

// 从 pagemap 读取 PFN
uint64_t get_pfn_from_pagemap(int pagemap_fd, uintptr_t vaddr) {
    if (pagemap_fd < 0) return 0;

    uint64_t page_index = vaddr / 4096;
    uint64_t offset = page_index * 8;

    uint64_t entry = 0;
    ssize_t n = pread(pagemap_fd, &entry, sizeof(entry), offset);

    if (n != sizeof(entry)) {
        return 0;
    }

    // 检查页是否存在
    if (!(entry & PAGE_PRESENT)) {
        return 0;
    }

    return entry & PFN_MASK;
}

// 处理单个客户端连接
void handle_client(int client_fd, pid_t target_pid) {
    char pagemap_path[64];
    snprintf(pagemap_path, sizeof(pagemap_path), "/proc/%d/pagemap", target_pid);

    int pagemap_fd = open(pagemap_path, O_RDONLY);
    if (pagemap_fd < 0) {
        fprintf(stderr, "[PFN Helper] Failed to open %s: %d\n", pagemap_path, errno);
        return;
    }

    printf("[PFN Helper] Client connected, pagemap opened: %s\n", pagemap_path);

    while (g_running) {
        uintptr_t vaddr;
        ssize_t n = recv(client_fd, &vaddr, sizeof(vaddr), 0);

        if (n <= 0) {
            // 客户端断开
            break;
        }

        // 查询 PFN
        uint64_t pfn = get_pfn_from_pagemap(pagemap_fd, vaddr);

        // 发送响应
        send(client_fd, &pfn, sizeof(pfn), 0);

        // 调试输出
        static int count = 0;
        if (count < 5) {
            printf("[PFN Helper] vaddr=0x%llx -> pfn=0x%llx\n",
                   (unsigned long long)vaddr, (unsigned long long)pfn);
            count++;
        }
    }

    close(pagemap_fd);
    printf("[PFN Helper] Client disconnected\n");
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <pid>\n", argv[0]);
        fprintf(stderr, "  <pid> - target process ID to monitor\n");
        return 1;
    }

    pid_t target_pid = atoi(argv[1]);
    if (target_pid <= 0) {
        fprintf(stderr, "Invalid PID: %s\n", argv[1]);
        return 1;
    }

    printf("[PFN Helper] Starting for PID %d...\n", target_pid);

    // 检查权限
    if (getuid() != 0) {
        fprintf(stderr, "[PFN Helper] WARNING: Not running as root! PFN query may fail.\n");
    }

    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 删除旧 socket 文件
    unlink(SOCKET_PATH);

    // 创建 Unix Socket
    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    // 设置权限，让 app 可以连接
    chmod(SOCKET_PATH, 0777);

    if (listen(server_fd, 5) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("[PFN Helper] Listening on %s\n", SOCKET_PATH);
    printf("[PFN Helper] Target PID: %d\n", target_pid);
    printf("[PFN Helper] Ready for connections...\n");

    // 主循环
    while (g_running) {
        struct sockaddr_un client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }

        printf("[PFN Helper] Client connected\n");
        handle_client(client_fd, target_pid);
        close(client_fd);
    }

    printf("[PFN Helper] Shutting down...\n");

    close(server_fd);
    unlink(SOCKET_PATH);

    return 0;
}