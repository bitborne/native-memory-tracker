// elf_sections.h - Section Header 解析
// 理解 so 文件布局的关键：.dynsym, .dynstr, .plt, .got.plt, .rela.plt

#ifndef ELF_SECTIONS_H
#define ELF_SECTIONS_H

#include "elf_types.h"
#include <vector>
#include <string>

// ========================================
// Section Header 类型常量
// ========================================
enum ElfSectionType : uint32_t {
    SHT_NULL = 0,           // 无效section
    SHT_PROGBITS = 1,       // 程序数据
    SHT_SYMTAB = 2,         // 符号表
    SHT_STRTAB = 3,         // 字符串表
    SHT_RELA = 4,           // 带加数的重定位
    SHT_HASH = 5,           // 符号哈希表
    SHT_DYNAMIC = 6,        // 动态链接信息
    SHT_NOTE = 7,           // 注释
    SHT_NOBITS = 8,         // 无数据（如.bss）
    SHT_REL = 9,            // 无加数的重定位
    SHT_SHLIB = 10,         // 保留
    SHT_DYNSYM = 11,        // 动态符号表
    SHT_INIT_ARRAY = 14,    // 构造函数指针数组
    SHT_FINI_ARRAY = 15,    // 析构函数指针数组
    SHT_PREINIT_ARRAY = 16, // 预构造函数
    SHT_GROUP = 17,         // Section组
    SHT_SYMTAB_SHNDX = 18,  // 扩展section索引
    SHT_GNU_HASH = 0x6ffffff6, // GNU哈希表（加速符号查找）
    SHT_ANDROID_REL = 0x60000001,
    SHT_ANDROID_RELA = 0x60000002
};

// Section flags
enum ElfSectionFlags : uint64_t {
    SHF_WRITE = 0x1,           // 可写
    SHF_ALLOC = 0x2,           // 运行时占用内存
    SHF_EXECINSTR = 0x4,       // 可执行
    SHF_MERGE = 0x10,          // 可合并
    SHF_STRINGS = 0x20,        // 包含空结尾字符串
    SHF_INFO_LINK = 0x40,      // sh_info包含section索引
    SHF_LINK_ORDER = 0x80,     // 保留链接顺序
    SHF_OS_NONCONFORMING = 0x100,
    SHF_GROUP = 0x200,
    SHF_TLS = 0x400,           // 线程本地存储
    SHF_MASKOS = 0x0ff00000,
    SHF_MASKPROC = 0xf0000000,
    SHF_ORDERED = 0x4000000,
    SHF_EXCLUDE = 0x8000000
};

// ========================================
// Section Header 结构 (64-bit)
// ========================================
struct Elf64_Shdr {
    uint32_t sh_name;       // Section名称在字符串表中的偏移
    uint32_t sh_type;       // Section类型
    uint64_t sh_flags;      // Section标志
    uint64_t sh_addr;       // 运行时虚拟地址
    uint64_t sh_offset;     // 文件偏移
    uint64_t sh_size;       // Section大小
    uint32_t sh_link;       // 链接到其他section的索引
    uint32_t sh_info;       // 额外信息
    uint64_t sh_addralign;  // 对齐要求
    uint64_t sh_entsize;    // 条目大小（如果section包含固定大小条目）
};

struct Elf32_Shdr {
    uint32_t sh_name;
    uint32_t sh_type;
    uint32_t sh_flags;
    uint32_t sh_addr;
    uint32_t sh_offset;
    uint32_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint32_t sh_addralign;
    uint32_t sh_entsize;
};

// ========================================
// Section 信息封装
// ========================================
struct SectionInfo {
    uint32_t index;         // Section索引
    std::string name;       // Section名称
    uint32_t type;          // Section类型
    uint64_t flags;         // Section标志
    uint64_t addr;          // 运行时地址
    uint64_t offset;        // 文件偏移
    uint64_t size;          // Section大小
    uint32_t link;          // 链接section
    uint32_t info;          // 额外信息
    uint64_t addralign;     // 对齐
    uint64_t entsize;       // 条目大小

    // 关键section快速判断
    bool isDynamicSymtab() const { return type == SHT_DYNSYM; }
    bool isSymtab() const { return type == SHT_SYMTAB; }
    bool isStrtab() const { return type == SHT_STRTAB; }
    bool isRela() const { return type == SHT_RELA; }
    bool isRel() const { return type == SHT_REL; }
    bool isDynamic() const { return type == SHT_DYNAMIC; }
    bool isHash() const { return type == SHT_HASH || type == SHT_GNU_HASH; }
    bool isExecutable() const { return flags & SHF_EXECINSTR; }
    bool isWritable() const { return flags & SHF_WRITE; }
    bool isAllocatable() const { return flags & SHF_ALLOC; }
};

// ========================================
// Section Header 表管理类
// ========================================
class SectionHeaderTable {
public:
    std::vector<SectionInfo> sections;
    const SectionInfo* shstrtabSection = nullptr;  // section名称字符串表
    const SectionInfo* dynstrSection = nullptr;    // 动态字符串表
    const SectionInfo* dynsymSection = nullptr;    // 动态符号表
    const SectionInfo* relaPltSection = nullptr;   // PLT重定位表
    const SectionInfo* relaDynSection = nullptr;   // 数据重定位表
    const SectionInfo* pltSection = nullptr;       // PLT代码
    const SectionInfo* gotPltSection = nullptr;    // GOT.PLT
    const SectionInfo* dynamicSection = nullptr;   // .dynamic
    const SectionInfo* hashSection = nullptr;      // .hash
    const SectionInfo* gnuHashSection = nullptr;   // .gnu.hash
    const SectionInfo* symtabSection = nullptr;    // .symtab（完整符号表）

private:
    bool is64bit_ = false;
    bool isLittleEndian_ = true;
    std::vector<uint8_t> shstrtabData_;  // section名称字符串表数据

public:
    // 解析 Section Header Table
    bool parse(const uint8_t* data, size_t size,
               uint64_t shoff, uint16_t shnum, uint16_t shentsize,
               uint16_t shstrndx, bool is64bit, bool isLittleEndian);

    // 获取section名称字符串表
    bool loadShstrtab(const uint8_t* data, size_t size);

    // 根据名称查找section
    const SectionInfo* findByName(const char* name) const;

    // 获取section在文件中的数据指针
    const uint8_t* getSectionData(const SectionInfo* sec, const uint8_t* fileData) const;

    // 从字符串表读取字符串
    static const char* getString(const uint8_t* strtab, size_t strtabSize, uint32_t offset);

    // 打印所有section（类似 readelf -S）
    void printSections() const;

    // 打印关键section摘要
    void printKeySections() const;

private:
    const char* getSectionTypeName(uint32_t type) const;
    std::string getSectionFlags(uint64_t flags) const;
};

#endif // ELF_SECTIONS_H