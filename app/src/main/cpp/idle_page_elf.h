//
// idle_page_elf.h
// ELF 节区解析 - 复用 elf_reader 模块
// 提供虚拟地址到节区名称的映射
//

#ifndef DEMO_SO_IDLE_PAGE_ELF_H
#define DEMO_SO_IDLE_PAGE_ELF_H

#include "elf_types.h"
#include "elf_sections.h"
#include "idle_page_mmap.h"

#include <cstdint>
#include <vector>
#include <string>
#include <map>

namespace idle_page {

// 节区映射项
struct SectionMapping {
    uintptr_t start;
    uintptr_t end;
    std::string name;
    std::string perms;  // 权限字符串 r-xp, rw-p 等
};

// ElfSectionMapper: 解析 SO 文件的节区信息
class ElfSectionMapper {
public:
    ElfSectionMapper();
    ~ElfSectionMapper();

    // 加载并解析 SO 文件
    bool load(const char* so_path);

    // 释放资源
    void unload();

    // 根据虚拟地址查找节区信息
    // 返回节区名称，如 ".text", ".data", ".bss" 等
    // 如果找不到，返回 "UNKNOWN"
    const char* lookup_section(uintptr_t vaddr) const;

    // 获取节区权限
    const char* lookup_perms(uintptr_t vaddr) const;

    // 打印节区映射（调试用）
    void dump_sections() const;

    // 检查是否已加载
    bool is_loaded() const { return !sections_.empty(); }

    // 获取所有节区映射
    const std::vector<SectionMapping>& get_sections() const { return sections_; }

private:
    // mmap 的 SO 文件数据
    int fd_ = -1;
    void* mmap_base_ = nullptr;
    size_t mmap_size_ = 0;

    // 节区映射表
    std::vector<SectionMapping> sections_;

    // 加载基址（需要与运行时 /proc/self/maps 对比确定）
    uintptr_t load_base_ = 0;

    // 解析 ELF 文件构建节区映射
    bool parse_elf();

    // 获取 section 的权限字符串
    std::string get_section_perms(uint32_t sh_flags);
};

// 运行时节区解析器（结合 /proc/self/maps 和 ELF 文件）
class RuntimeSectionResolver {
public:
    // 初始化：解析 SO 文件的节区，结合运行时 maps 信息
    bool init(const char* so_name);

    // 查找地址对应的节区信息
    // 返回格式: ".text (r-xp)" 或 ".data (rw-p)"
    std::string resolve(uintptr_t vaddr) const;

    // 单独获取名称和权限
    const char* get_name(uintptr_t vaddr) const;
    const char* get_perms(uintptr_t vaddr) const;

private:
    ElfSectionMapper elf_mapper_;
    std::vector<MemoryRegion> runtime_regions_;

    // 计算 ELF 文件的加载基址
    bool calculate_load_base();
};

} // namespace idle_page

#endif // DEMO_SO_IDLE_PAGE_ELF_H