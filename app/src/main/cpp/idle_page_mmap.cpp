//
// idle_page_mmap.cpp
// Mmap 封装实现
//

#include "idle_page_mmap.h"
#include "idle_page_log.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <cstring>
#include <cstdio>

namespace idle_page {

// ==================== MmapPagemap ====================

MmapPagemap::MmapPagemap() = default;

MmapPagemap::~MmapPagemap() {
    close();
}

bool MmapPagemap::open() {
    if (fd_ >= 0) return true;

    fd_ = ::open("/proc/self/pagemap", O_RDONLY);
    if (fd_ < 0) {
        LOGE("[IdlePage] Failed to open /proc/self/pagemap");
        return false;
    }

    // 获取文件大小
    struct stat st;
    if (fstat(fd_, &st) < 0) {
        LOGE("[IdlePage] fstat pagemap failed");
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
        LOGI("[IdlePage] pagemap mmap failed, using pread fallback");
    }

    LOGI("[IdlePage] Pagemap opened (fd=%d)", fd_);
    return true;
}

void MmapPagemap::close() {
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
    if (fd_ < 0) return 0;

    // 计算页框索引
    uint64_t page_index = vaddr / 4096;
    uint64_t offset = page_index * 8;  // 每个条目 8 字节

    uint64_t entry = 0;

    if (mmap_base_) {
        // 检查是否在缓存范围内
        if (offset < mmap_size_) {
            entry = *reinterpret_cast<const uint64_t*>(
                static_cast<const uint8_t*>(mmap_base_) + offset);
        } else {
            // 超出缓存，使用 pread
            pread(fd_, &entry, sizeof(entry), offset);
        }
    } else {
        // 回退到 pread
        pread(fd_, &entry, sizeof(entry), offset);
    }

    // 检查页是否存在
    if (!(entry & PAGE_PRESENT)) {
        return 0;
    }

    return entry & PFN_MASK;
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
        LOGE("[IdlePage] Failed to open page_idle/bitmap (need root?)");
        return false;
    }

    LOGI("[IdlePage] PageIdle opened (fd=%d)", fd_);
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

void MmapPageIdle::load_cache(uint64_t block_idx) {
    if (cached_block_idx_ == block_idx) return;

    // 如果当前缓存脏，先刷新
    if (cache_dirty_) {
        flush_cache();
    }

    uint64_t offset = block_idx * CACHE_SIZE;
    ssize_t n = pread(fd_, cache_, CACHE_SIZE, offset);

    if (n < 0) {
        LOGE("[IdlePage] Failed to read bitmap block %llu", block_idx);
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
        LOGE("[IdlePage] Failed to open /proc/self/maps");
        return false;
    }

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        // 检查是否包含目标 so 名称
        if (strstr(line, so_name)) {
            MemoryRegion region;
            if (parse_line(line, region)) {
                region.is_monitor = true;
                regions.push_back(region);
                LOGI("[IdlePage] Found region: 0x%llx-0x%llx %s %s",
                     region.start, region.end, region.perms, region.name.c_str());
            }
        }
    }

    fclose(fp);

    if (regions.empty()) {
        LOGE("[IdlePage] No regions found for %s", so_name);
        return false;
    }

    return true;
}

bool ProcMapsParser::get_all_regions(std::vector<MemoryRegion>& regions) {
    regions.clear();

    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) {
        LOGE("[IdlePage] Failed to open /proc/self/maps");
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