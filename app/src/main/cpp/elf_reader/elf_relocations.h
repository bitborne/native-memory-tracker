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

public:
    // 解析重定位表
    // data: 重定位表数据指针（.rela.plt 或 .rela.dyn）
    // size: 重定位表大小
    bool parse(const uint8_t* data, size_t size,
               bool is64bit, bool isLittleEndian, bool isPLT);

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

    // 获取重定位类型名称
    static const char* getTypeName(uint32_t type);

    // 判断是否是 PLT 相关重定位
    static bool isPLTReloc(uint32_t type);
};

#endif // ELF_RELOCATIONS_H