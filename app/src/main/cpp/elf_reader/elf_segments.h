// elf_segments.h - Program Header (Segment) 解析
// 理解运行时内存布局：PT_LOAD 段的加载

#ifndef ELF_SEGMENTS_H
#define ELF_SEGMENTS_H

#include "elf_types.h"
#include <vector>
#include <string>

// ========================================
// Program Header 类型常量 (p_type)
// ========================================
enum ElfSegmentType : uint32_t {
    PT_NULL    = 0,            // 未使用
    PT_LOAD    = 1,            // 可加载段（最重要的类型！）
    PT_DYNAMIC = 2,            // 动态链接信息
    PT_INTERP  = 3,            // 解释器路径（如 /system/bin/linker64）
    PT_NOTE    = 4,            // 辅助信息
    PT_SHLIB   = 5,            // 保留（共享库特定）
    PT_PHDR    = 6,            // 程序头表自身
    PT_TLS     = 7,            // 线程本地存储
    PT_LOOS    = 0x60000000,   // OS 特定类型开始
    PT_HIOS    = 0x6fffffff,   // OS 特定类型结束
    PT_LOPROC  = 0x70000000,   // 处理器特定类型开始
    PT_HIPROC  = 0x7fffffff,   // 处理器特定类型结束
    PT_GNU_EH_FRAME = 0x6474e550,  // GNU .eh_frame 段
    PT_GNU_STACK    = 0x6474e551,  // GNU 栈标志
    PT_GNU_RELRO    = 0x6474e552,  // GNU 只读重定位
};

// ========================================
// Program Header 标志 (p_flags)
// ========================================
enum ElfSegmentFlags : uint32_t {
    PF_X = 0x1,                // 可执行
    PF_W = 0x2,                // 可写
    PF_R = 0x4,                // 可读
    PF_MASKOS   = 0x0ff00000,  // OS 特定
    PF_MASKPROC = 0xf0000000,  // 处理器特定
};

// ========================================
// Program Header 结构 (64-bit)
// ========================================
struct Elf64_Phdr {
    uint32_t p_type;           // 段类型
    uint32_t p_flags;          // 段标志
    uint64_t p_offset;         // 文件偏移
    uint64_t p_vaddr;          // 虚拟地址（运行时地址）
    uint64_t p_paddr;          // 物理地址（通常忽略）
    uint64_t p_filesz;         // 文件大小
    uint64_t p_memsz;          // 内存大小（>= 文件大小，.bss 扩展）
    uint64_t p_align;          // 对齐要求
};

struct Elf32_Phdr {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
};

// ========================================
// Segment 信息封装
// ========================================
struct SegmentInfo {
    uint32_t index;            // 在程序头表中的索引
    uint32_t type;             // 段类型
    uint32_t flags;            // 段标志
    uint64_t offset;           // 文件偏移
    uint64_t vaddr;            // 虚拟地址
    uint64_t paddr;            // 物理地址
    uint64_t filesz;           // 文件大小
    uint64_t memsz;            // 内存大小
    uint64_t align;            // 对齐

    // 辅助判断
    bool isLoad() const { return type == PT_LOAD; }
    bool isDynamic() const { return type == PT_DYNAMIC; }
    bool isInterp() const { return type == PT_INTERP; }
    bool isExecutable() const { return flags & PF_X; }
    bool isWritable() const { return flags & PF_W; }
    bool isReadable() const { return flags & PF_R; }

    // 获取权限字符串
    std::string getFlagsString() const {
        std::string s = "   ";
        s[0] = (flags & PF_R) ? 'R' : ' ';
        s[1] = (flags & PF_W) ? 'W' : ' ';
        s[2] = (flags & PF_X) ? 'E' : ' ';
        return s;
    }
};

// ========================================
// Program Header 表管理类
// ========================================
class ProgramHeaderTable {
public:
    std::vector<SegmentInfo> segments;
    std::vector<const SegmentInfo*> loadSegments;  // PT_LOAD 段列表
    const SegmentInfo* dynamicSegment = nullptr;     // PT_DYNAMIC
    const SegmentInfo* interpSegment = nullptr;      // PT_INTERP
    const SegmentInfo* phdrSegment = nullptr;        // PT_PHDR

private:
    bool is64bit_ = false;
    bool isLittleEndian_ = true;

public:
    // 解析 Program Header Table
    // data: ELF 文件数据
    // phoff: Program Header 在文件中的偏移 (e_phoff)
    // phnum: Program Header 条目数量 (e_phnum)
    // phentsize: 每个 Program Header 的大小 (e_phentsize)
    bool parse(const uint8_t* data, size_t size,
               uint64_t phoff, uint16_t phnum, uint16_t phentsize,
               bool is64bit, bool isLittleEndian);

    // 根据类型查找段
    const SegmentInfo* findByType(uint32_t type) const;

    // 查找包含给定虚拟地址的 PT_LOAD 段
    const SegmentInfo* findLoadSegmentByVAddr(uint64_t vaddr) const;

    // 打印所有段
    void print() const;

    // 只打印 PT_LOAD 段
    void printLoadSegments() const;

    // 获取段类型名称
    static const char* getTypeName(uint32_t type);

    // 获取段描述
    static const char* getTypeDescription(uint32_t type);
};

#endif // ELF_SEGMENTS_H
