// elf_relocations.h - 重定位表解析
// .rela.plt 和 .rela.dyn 的结构定义与解析
// 理解函数跳转的关键：重定位条目如何连接符号表和 GOT

#ifndef ELF_RELOCATIONS_H
#define ELF_RELOCATIONS_H

#include "elf_types.h"
#include "elf_sections.h"
#include "elf_symbols.h"
#include <vector>

// ========================================
// ARM64 重定位类型
// ========================================
enum ElfAArch64Reloc : uint32_t {
    R_AARCH64_NONE = 0,              // 无操作
    R_AARCH64_ABS64 = 257,           // 绝对 64 位地址
    R_AARCH64_COPY = 1024,           // 复制数据
    R_AARCH64_GLOB_DAT = 1025,       // 全局数据
    R_AARCH64_JUMP_SLOT = 1026,      // PLT 跳转槽（最重要！）
    R_AARCH64_RELATIVE = 1027,       // 相对基地址
    R_AARCH64_TLS_DTPREL64 = 1028,   // TLS 相关
    R_AARCH64_TLS_TPREL64 = 1029,
    R_AARCH64_TLSDESC = 1031,
    R_AARCH64_IRELATIVE = 1032
};

// ========================================
// x86_64 重定位类型
// ========================================
enum ElfX86_64Reloc : uint32_t {
    R_X86_64_NONE = 0,               // 无操作
    R_X86_64_64 = 1,                 // 绝对 64 位地址
    R_X86_64_PC32 = 2,               // PC 相对 32 位
    R_X86_64_GOT32 = 3,              // GOT 相对 32 位
    R_X86_64_PLT32 = 4,              // PLT 相对 32 位
    R_X86_64_COPY = 5,               // 复制数据
    R_X86_64_GLOB_DAT = 6,           // 全局数据
    R_X86_64_JUMP_SLOT = 7,          // PLT 跳转槽（最重要！）
    R_X86_64_RELATIVE = 8,           // 相对基地址
    R_X86_64_GOTPCREL = 9,           // GOT PC 相对
    R_X86_64_32 = 10,                // 绝对 32 位
    R_X86_64_32S = 11,               // 有符号 32 位
    R_X86_64_16 = 12,                // 绝对 16 位
    R_X86_64_PC16 = 13,              // PC 相对 16 位
    R_X86_64_8 = 14,                 // 绝对 8 位
    R_X86_64_PC8 = 15,               // PC 相对 8 位
    R_X86_64_DTPMOD64 = 16,          // TLS 相关
    R_X86_64_DTPOFF64 = 17,
    R_X86_64_TPOFF64 = 18,
    R_X86_64_IRELATIVE = 37          // 间接相对
};

// ========================================
// 重定位表条目结构 (64-bit)
// ========================================
struct Elf64_Rela {
    uint64_t r_offset;   // 重定位位置（GOT 条目的地址）
    uint64_t r_info;     // 符号索引 + 重定位类型
    int64_t  r_addend;   // 加数（用于计算最终地址）
};

// ========================================
// 重定位信息封装
// ========================================
struct RelocationInfo {
    uint32_t index;         // 在重定位表中的索引
    uint64_t offset;        // GOT 条目的运行时地址
    uint64_t info;          // 原始 r_info
    uint32_t symIndex;      // 符号表索引
    uint32_t type;          // 重定位类型
    int64_t  addend;        // 加数

    // 运行时填充
    const SymbolInfo* symbol = nullptr;  // 指向对应的符号
};

// ========================================
// 重定位表管理类
// ========================================
class RelocationTable {
public:
    std::vector<RelocationInfo> relocations;
    bool isPLT = false;  // true = .rela.plt, false = .rela.dyn

private:
    bool is64bit_ = false;
    bool isLittleEndian_ = true;
    uint32_t machine_ = 0;  // ELF 机器类型 (183=ARM64, 62=X86_64)

public:
    // 解析重定位表
    // data: 重定位表数据指针（.rela.plt 或 .rela.dyn）
    // size: 重定位表大小
    // machine: ELF header 的 e_machine 字段，用于确定重定位类型名称
    bool parse(const uint8_t* data, size_t size,
               bool is64bit, bool isLittleEndian, bool isPLT, uint32_t machine = 0);

    // 设置机器类型（用于获取正确的重定位类型名称）
    void setMachine(uint32_t machine) { machine_ = machine; }

    // 关联符号表（解析后调用，建立 symbol 指针）
    void linkSymbols(const DynamicSymbolTable& symtab);

    // 根据符号名查找重定位条目
    const RelocationInfo* findBySymbolName(const char* name) const;

    // 根据 GOT 索引查找重定位条目
    const RelocationInfo* findByGOTIndex(uint32_t gotIndex) const;

    // 计算 GOT 索引（从 offset 计算）
    // gotStart: .got.plt 的起始地址
    uint32_t getGOTIndex(const RelocationInfo* rel, uint64_t gotStart) const;

    // 打印所有重定位条目（类似 readelf -r）
    void printRelocations() const;

    // 打印 PLT 重定位（仅函数跳转）
    void printPLTRelocations() const;

    // 获取重定位类型名称（根据 machine 类型自动选择）
    const char* getTypeName(uint32_t type) const;

    // 获取 ARM64 重定位类型名称
    static const char* getTypeNameAArch64(uint32_t type);

    // 获取 x86_64 重定位类型名称
    static const char* getTypeNameX86_64(uint32_t type);

    // 判断是否是 PLT 相关重定位
    static bool isPLTReloc(uint32_t type, uint32_t machine = 0);
};

#endif // ELF_RELOCATIONS_H