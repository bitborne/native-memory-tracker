//
// idle_page_mmap.cpp
// Mmap 封装实现 - 支持 PFN Helper 模式
//

#include "idle_page_mmap.h"
#include "idle_page_log.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <cstring>
#include <cstdio>
#include <cinttypes>
#include <cstdlib>
#include <errno.h>

#define PFN_HELPER_SOCKET "/data/local/tmp/pfn_helper.sock"

namespace idle_page {

// ==================== MmapPagemap ====================

MmapPagemap::MmapPagemap() = default;

MmapPagemap::~MmapPagemap() {
    close();
}

bool MmapPagemap::open() {
    // 首先尝试连接 PFN Helper
    helper_fd_ = connect_to_helper();
    if (helper_fd_ >= 0) {
        IDLE_LOGI("[IdlePage] Connected to PFN Helper (fd=%d)", helper_fd_);
        use_helper_ = true;
        return true;
    }

    // 回退到本地 pagemap
    fd_ = ::open("/proc/self/pagemap", O_RDONLY);
    if (fd_ < 0) {
        IDLE_LOGE("[IdlePage] Failed to open /proc/self/pagemap");
        return false;
    }

    // 获取文件大小
    struct stat st;
    if (fstat(fd_, &st) < 0) {
        IDLE_LOGE("[IdlePage] fstat pagemap failed");
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    // /proc/self/pagemap 是特殊文件，无法真正 mmap 整个文件
    // 采用按需读取策略，使用页缓存
    mmap_size_ = 4096;  // 4KB 缓存
    mmap_base_ = mmap(nullptr, mmap_size_, PROT_READ, MAP_PRIVATE, fd_, 0);

    if (mmap_base_ == MAP_FAILED) {
        // mmap 失败，回退到 pread（但尽量避免）
        mmap_base_ = nullptr;
        IDLE_LOGE("[IdlePage] pagemap mmap failed, using pread fallback");
    }

    IDLE_LOGI("[IdlePage] Pagemap opened (fd=%d)", fd_);
    return true;
}

// 连接到 PFN Helper
int MmapPagemap::connect_to_helper() {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, PFN_HELPER_SOCKET, sizeof(addr.sun_path) - 1);

    if (::connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ::close(sock);
        return -1;
    }

    return sock;
}

void MmapPagemap::close() {
    if (helper_fd_ >= 0) {
        ::close(helper_fd_);
        helper_fd_ = -1;
    }
    if (mmap_base_) {
        munmap(mmap_base_, mmap_size_);
        mmap_base_ = nullptr;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

uint64_t MmapPagemap::get_pfn(uintptr_t vaddr) const {
    if (use_helper_ && helper_fd_ >= 0) {
        return get_pfn_from_helper(vaddr);
    }

    if (fd_ < 0) return 0;

    // 计算页框索引
    uint64_t page_index = vaddr / 4096;
    uint64_t offset = page_index * 8;  // 每个条目 8 字节

    uint64_t entry = 0;
    ssize_t n = 0;

    if (mmap_base_) {
        // 检查是否在缓存范围内
        if (offset < mmap_size_) {
            entry = *reinterpret_cast<const uint64_t*>(
                static_cast<const uint8_t*>(mmap_base_) + offset);
        } else {
            // 超出缓存，使用 pread
            n = pread(fd_, &entry, sizeof(entry), offset);
        }
    } else {
        // 回退到 pread
        n = pread(fd_, &entry, sizeof(entry), offset);
    }

    if (n < 0) {
        IDLE_LOGE("[IdlePage] pread pagemap failed: errno=%d", errno);
        return 0;
    }

    // 调试：打印前几个页面的读取结果
    static int debug_count = 0;
    if (debug_count < 5) {
        IDLE_LOGI("[IdlePage] get_pfn: vaddr=0x%llx offset=%llu n=%zd entry=0x%016llx",
                  (unsigned long long)vaddr, (unsigned long long)offset, n, (unsigned long long)entry);
        debug_count++;
    }

    // 检查页是否存在
    if (!(entry & PAGE_PRESENT)) {
        return 0;
    }

    return entry & PFN_MASK;
}

// 从 PFN Helper 查询
uint64_t MmapPagemap::get_pfn_from_helper(uintptr_t vaddr) const {
    if (helper_fd_ < 0) return 0;

    // 发送虚拟地址
    uintptr_t req = vaddr;
    ssize_t n = send(helper_fd_, &req, sizeof(req), 0);
    if (n != sizeof(req)) {
        IDLE_LOGE("[IdlePage] Failed to send request to helper: %zd", n);
        return 0;
    }

    // 接收 PFN
    uint64_t pfn = 0;
    n = recv(helper_fd_, &pfn, sizeof(pfn), 0);
    if (n != sizeof(pfn)) {
        IDLE_LOGE("[IdlePage] Failed to receive response from helper: %zd", n);
        return 0;
    }

    // 调试输出
    static int debug_count = 0;
    if (debug_count < 5) {
        IDLE_LOGI("[IdlePage] get_pfn_from_helper: vaddr=0x%llx -> pfn=0x%llx",
                  (unsigned long long)vaddr, (unsigned long long)pfn);
        debug_count++;
    }

    return pfn;
}

// ==================== MmapPageIdle ====================

MmapPageIdle::MmapPageIdle() {
    memset(cache_, 0, sizeof(cache_));
}

MmapPageIdle::~MmapPageIdle() {
    close();
}

bool MmapPageIdle::open() {
    if (fd_ >= 0) return true;

    fd_ = ::open("/sys/kernel/mm/page_idle/bitmap", O_RDWR);
    if (fd_ < 0) {
        IDLE_LOGE("[IdlePage] Failed to open page_idle/bitmap (need root?)");
        return false;
    }

    IDLE_LOGI("[IdlePage] PageIdle opened (fd=%d)", fd_);
    return true;
}

void MmapPageIdle::close() {
    if (cache_dirty_) {
        flush_cache();
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    cached_block_idx_ = UINT64_MAX;
}

// 检查是否有 root 权限
bool MmapPageIdle::check_root_access() {
    // 方法1: 尝试直接打开 page_idle/bitmap
    int fd = ::open("/sys/kernel/mm/page_idle/bitmap", O_RDWR);
    if (fd >= 0) {
        ::close(fd);
        return true;
    }

    // 方法2: 检查 uid 是否为 0
    if (getuid() == 0) {
        return true;
    }

    // 方法3: 检查能否访问 su 命令，并尝试获取 root
    if (access("/system/bin/su", X_OK) == 0 ||
        access("/system/xbin/su", X_OK) == 0 ||
        access("/su/bin/su", X_OK) == 0) {
        // 尝试执行 su 获取 root 权限
        int ret = system("su -c 'id' > /dev/null 2>&1");
        if (ret == 0) {
            // 重新检查 uid
            if (getuid() == 0) {
                IDLE_LOGI("[IdlePage] GET ROOT!");
                return true;
            }
        }
        IDLE_LOGE("[IdlePage] GET root FAILED!");
    }

    return false;
}

void MmapPageIdle::load_cache(uint64_t block_idx) {
    if (cached_block_idx_ == block_idx) return;

    // 如果当前缓存脏，先刷新
    if (cache_dirty_) {
        flush_cache();
    }

    uint64_t offset = block_idx * CACHE_SIZE;
    ssize_t n = pread(fd_, cache_, CACHE_SIZE, offset);

    if (n < 0) {
        IDLE_LOGE("[IdlePage] Failed to read bitmap block %" PRIu64, block_idx);
        memset(cache_, 0, CACHE_SIZE);
    } else if (n < CACHE_SIZE) {
        // 部分读取，清零剩余部分
        memset(cache_ + n, 0, CACHE_SIZE - n);
    }

    cached_block_idx_ = block_idx;
    cache_dirty_ = false;
}

void MmapPageIdle::flush_cache() {
    if (!cache_dirty_ || cached_block_idx_ == UINT64_MAX) return;

    uint64_t offset = cached_block_idx_ * CACHE_SIZE;
    pwrite(fd_, cache_, CACHE_SIZE, offset);

    cache_dirty_ = false;
}

void MmapPageIdle::set_idle(uint64_t pfn) {
    if (fd_ < 0) return;

    uint64_t block_idx = get_bitmap_index(pfn) / (CACHE_SIZE / 8);
    uint32_t idx_in_block = (get_bitmap_index(pfn) % (CACHE_SIZE / 8));
    uint32_t bit_off = get_bit_offset(pfn);

    load_cache(block_idx);

    uint64_t* word = reinterpret_cast<uint64_t*>(cache_) + idx_in_block;
    *word |= (1ULL << bit_off);  // 设置 bit 为 1（idle）

    cache_dirty_ = true;
}

bool MmapPageIdle::is_accessed(uint64_t pfn) {
    if (fd_ < 0) return false;

    uint64_t block_idx = get_bitmap_index(pfn) / (CACHE_SIZE / 8);
    uint32_t idx_in_block = (get_bitmap_index(pfn) % (CACHE_SIZE / 8));
    uint32_t bit_off = get_bit_offset(pfn);

    load_cache(block_idx);

    uint64_t* word = reinterpret_cast<uint64_t*>(cache_) + idx_in_block;
    // bit = 0: 被访问过（硬件设置了 Accessed bit）
    // bit = 1: 保持 idle
    return (*word & (1ULL << bit_off)) == 0;
}

void MmapPageIdle::set_idle_batch(const std::vector<uint64_t>& pfns) {
    // 按块分组，减少缓存刷新
    for (uint64_t pfn : pfns) {
        set_idle(pfn);
    }
    // 最后刷新缓存
    if (cache_dirty_) {
        flush_cache();
    }
}

void MmapPageIdle::check_accessed_batch(const std::vector<uint64_t>& pfns,
                                        std::vector<bool>& results) {
    results.resize(pfns.size());

    for (size_t i = 0; i < pfns.size(); ++i) {
        results[i] = is_accessed(pfns[i]);
    }
}

// ==================== ProcMapsParser ====================

bool ProcMapsParser::parse_line(const char* line, MemoryRegion& region) {
    char perms[8];
    char path[256] = {0};
    unsigned long start, end;
    unsigned long offset, inode;
    int dev_major, dev_minor;

    // 格式: start-end perms offset major:minor inode path
    int n = sscanf(line, "%lx-%lx %7s %lx %x:%x %lu %255[^\n]",
                   &start, &end, perms, &offset,
                   &dev_major, &dev_minor, &inode, path);

    if (n < 2) return false;

    region.start = start;
    region.end = end;
    strncpy(region.perms, perms, 4);
    region.perms[4] = '\0';
    region.offset = (n >= 4) ? offset : 0;  // 解析偏移量
    region.name = path;

    // 去除 path 的前后空格
    size_t len = region.name.length();
    size_t first = region.name.find_first_not_of(" \t");
    if (first != std::string::npos) {
        region.name = region.name.substr(first, len - first);
    }

    return true;
}

bool ProcMapsParser::find_so_regions(const char* so_name,
                                     std::vector<MemoryRegion>& regions) {
    regions.clear();

    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) {
        IDLE_LOGE("[IdlePage] Failed to open /proc/self/maps");
        return false;
    }

    char line[512];
    bool found_any = false;
    while (fgets(line, sizeof(line), fp)) {
        // 调试：打印所有包含 "demo" 的行
//        if (strstr(line, "demo") || strstr(line, "libdemo")) {
//            IDLE_LOGI("[IdlePage] Maps line: %s", line);
//        }
        // 检查是否包含目标 so 名称
        if (strstr(line, so_name)) {
            IDLE_LOGI("[IdlePage] Matched so_name '%s' in line: %s", so_name, line);
            MemoryRegion region;
            if (parse_line(line, region)) {
                region.is_monitor = true;
                regions.push_back(region);
                IDLE_LOGI("[IdlePage] Found region: 0x%" PRIxPTR "-0x%" PRIxPTR " %s %s",
                     region.start, region.end, region.perms, region.name.c_str());
                found_any = true;
            } else {
                IDLE_LOGE("[IdlePage] parse_line failed for: %s", line);
            }
        }
    }

    // 回退方案：如果从 APK 加载（Android 6.0+），尝试匹配 base.apk 的可执行区域
    if (regions.empty() && strstr(so_name, "demo_so")) {
        IDLE_LOGI("[IdlePage] Fallback: searching for base.apk regions");
        rewind(fp);
        while (fgets(line, sizeof(line), fp)) {
            // 匹配 base.apk 中的 r-xp（代码段）或 rw-p（数据段）区域
            if (strstr(line, "base.apk") && (strstr(line, "r-xp") || strstr(line, "rw-p"))) {
                MemoryRegion region;
                if (parse_line(line, region)) {
                    region.is_monitor = true;
                    regions.push_back(region);
                    IDLE_LOGI("[IdlePage] Found APK region: 0x%" PRIxPTR "-0x%" PRIxPTR " %s",
                         region.start, region.end, region.perms);
                    found_any = true;
                }
            }
        }
    }

    fclose(fp);

    if (regions.empty()) {
        IDLE_LOGE("[IdlePage] No regions found for %s", so_name);
        return false;
    }

    return true;
}

bool ProcMapsParser::get_all_regions(std::vector<MemoryRegion>& regions) {
    regions.clear();

    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) {
        IDLE_LOGE("[IdlePage] Failed to open /proc/self/maps");
        return false;
    }

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        MemoryRegion region;
        if (parse_line(line, region)) {
            regions.push_back(region);
        }
    }

    fclose(fp);
    return true;
}

} // namespace idle_page