//
// idle_page_mmap.h
// 使用 mmap 高效访问 /proc/self/pagemap 和 /sys/kernel/mm/page_idle/bitmap
//

#ifndef DEMO_SO_IDLE_PAGE_MMAP_H
#define DEMO_SO_IDLE_PAGE_MMAP_H

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

namespace idle_page {

// 内存区域信息（来自 /proc/self/maps）
struct MemoryRegion {
    uintptr_t start;
    uintptr_t end;
    char perms[5];      // r-xp, rw-p 等
    std::string name;   // 节区名称如 ".text", ".data" 或路径
    bool is_monitor;    // 是否属于监控目标
};

// 页信息
struct PageInfo {
    uintptr_t vaddr;      // 虚拟地址
    uint64_t pfn;         // 物理页帧号
    uint32_t region_idx;  // 对应 MemoryRegions 的索引
    bool is_accessible;   // 是否可监控（有有效 PFN）
};

// MmapPagemap: 通过 mmap 访问 pagemap（替代 pread）
class MmapPagemap {
public:
    MmapPagemap();
    ~MmapPagemap();

    // 打开并 mmap /proc/self/pagemap
    bool open();
    void close();

    // 获取虚拟地址对应的 PFN
    // 返回 0 表示无效
    uint64_t get_pfn(uintptr_t vaddr) const;

    bool is_open() const { return mmap_base_ != nullptr; }

private:
    int fd_ = -1;
    void* mmap_base_ = nullptr;
    size_t mmap_size_ = 0;

    static constexpr uint64_t PFN_MASK = (1ULL << 55) - 1;
    static constexpr uint64_t PAGE_PRESENT = (1ULL << 63);
};

// MmapPageIdle: 通过 mmap 访问 page_idle bitmap
class MmapPageIdle {
public:
    MmapPageIdle();
    ~MmapPageIdle();

    // 打开 /sys/kernel/mm/page_idle/bitmap
    // 注意：这个文件不能真正 mmap，使用缓存块策略
    bool open();
    void close();

    // 设置指定 PFN 为 idle 状态（清除 Accessed bit）
    void set_idle(uint64_t pfn);

    // 批量设置 idle（优化：连续 PFN 批量写）
    void set_idle_batch(const std::vector<uint64_t>& pfns);

    // 检查 PFN 是否被访问过
    // 返回 true = 被访问过（bitmap bit = 0）
    // 返回 false = idle（bitmap bit = 1）
    bool is_accessed(uint64_t pfn);

    // 批量读取访问状态
    // 传入 pfns 列表，返回对应的 accessed 状态（true=访问过）
    void check_accessed_batch(const std::vector<uint64_t>& pfns,
                              std::vector<bool>& results);

    bool is_open() const { return fd_ >= 0; }

private:
    int fd_ = -1;

    // 缓存当前 bitmap 块（8KB = 64K pages）
    static constexpr size_t CACHE_SIZE = 8 * 1024;  // 8KB
    uint8_t cache_[CACHE_SIZE];
    uint64_t cached_block_idx_ = UINT64_MAX;
    bool cache_dirty_ = false;

    // 辅助函数
    uint64_t get_bitmap_offset(uint64_t pfn) const {
        return (pfn / 64) * 8;  // 每 64 个页一个 uint64_t，每个 uint64_t 8 字节
    }

    uint64_t get_bitmap_index(uint64_t pfn) const {
        return pfn / 64;
    }

    uint32_t get_bit_offset(uint64_t pfn) const {
        return pfn % 64;
    }

    void flush_cache();
    void load_cache(uint64_t block_idx);
};

// ProcMapsParser: 解析 /proc/self/maps
class ProcMapsParser {
public:
    // 查找指定 so 文件的内存区域
    // 返回所有匹配的 MemoryRegion
    static bool find_so_regions(const char* so_name,
                                std::vector<MemoryRegion>& regions);

    // 获取当前进程的所有区域
    static bool get_all_regions(std::vector<MemoryRegion>& regions);

private:
    static bool parse_line(const char* line, MemoryRegion& region);
};

} // namespace idle_page

#endif // DEMO_SO_IDLE_PAGE_MMAP_H