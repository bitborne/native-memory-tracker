// elf_relocations.cpp - 重定位表解析实现

#include "elf_relocations.h"
#include <cstdio>

// ========================================
// RelocationTable 实现
// ========================================

bool RelocationTable::parse(const uint8_t* data, size_t size,
                            bool is64bit, bool isLittleEndian, bool isPLT) {
    is64bit_ = is64bit;
    isLittleEndian_ = isLittleEndian;
    this->isPLT = isPLT;

    if (!data || size == 0) {
        fprintf(stderr, "Error: No relocation table data\n");
        return false;
    }

    // 计算重定位条目数量
    size_t entrySize = is64bit ? sizeof(Elf64_Rela) : 0;  // 只支持 64-bit 的 Rela
    if (entrySize == 0) {
        fprintf(stderr, "Error: 32-bit relocation not yet supported\n");
        return false;
    }

    size_t numEntries = size / entrySize;

    if (numEntries == 0) {
        fprintf(stderr, "Warning: Empty relocation table\n");
        return true;
    }

    relocations.reserve(numEntries);

    for (size_t i = 0; i < numEntries; i++) {
        const uint8_t* entry = data + i * entrySize;

        RelocationInfo rel;
        rel.index = static_cast<uint32_t>(i);

        // Elf64_Rela 布局：
        // offset 0: r_offset (8 bytes)
        // offset 8: r_info (8 bytes)
        // offset 16: r_addend (8 bytes)
        // 总计：24 bytes
        rel.offset = readVal<uint64_t>(entry + 0, isLittleEndian);
        rel.info = readVal<uint64_t>(entry + 8, isLittleEndian);
        rel.addend = static_cast<int64_t>(readVal<uint64_t>(entry + 16, isLittleEndian));

        // 解码 r_info
        // 高 32 位：符号表索引
        // 低 32 位：重定位类型
        rel.symIndex = static_cast<uint32_t>(rel.info >> 32);
        rel.type = static_cast<uint32_t>(rel.info & 0xffffffff);

        relocations.push_back(rel);
    }

    return true;
}

void RelocationTable::linkSymbols(const DynamicSymbolTable& symtab) {
    for (auto& rel : relocations) {
        rel.symbol = symtab.findByIndex(rel.symIndex);
    }
}

const RelocationInfo* RelocationTable::findBySymbolName(const char* name) const {
    for (const auto& rel : relocations) {
        if (rel.symbol && rel.symbol->name == name) {
            return &rel;
        }
    }
    return nullptr;
}

const RelocationInfo* RelocationTable::findByGOTIndex(uint32_t gotIndex) const {
    // GOT 索引与重定位表索引的对应关系：
    // rela[0] -> GOT[3] (第一个函数)
    // rela[n] -> GOT[n+3]
    uint32_t relIndex = gotIndex >= 3 ? gotIndex - 3 : gotIndex;

    if (relIndex < relocations.size()) {
        return &relocations[relIndex];
    }
    return nullptr;
}

uint32_t RelocationTable::getGOTIndex(const RelocationInfo* rel, uint64_t gotStart) const {
    if (!rel) return 0;

    // 计算 GOT 索引
    // offset 是 GOT 条目的运行时地址
    // gotStart 是 .got.plt 的起始地址
    // 每个 GOT 条目是 8 字节（64位指针）
    uint64_t offset = rel->offset;
    return static_cast<uint32_t>((offset - gotStart) / 8);
}

const char* RelocationTable::getTypeName(uint32_t type) {
    switch (type) {
        case R_AARCH64_NONE:         return "R_AARCH64_NONE";
        case R_AARCH64_ABS64:        return "R_AARCH64_ABS64";
        case R_AARCH64_COPY:         return "R_AARCH64_COPY";
        case R_AARCH64_GLOB_DAT:     return "R_AARCH64_GLOB_DAT";
        case R_AARCH64_JUMP_SLOT:    return "R_AARCH64_JUMP_SLOT";
        case R_AARCH64_RELATIVE:     return "R_AARCH64_RELATIVE";
        case R_AARCH64_TLS_DTPREL64: return "R_AARCH64_TLS_DTPREL64";
        case R_AARCH64_TLS_TPREL64:  return "R_AARCH64_TLS_TPREL64";
        case R_AARCH64_TLSDESC:      return "R_AARCH64_TLSDESC";
        case R_AARCH64_IRELATIVE:    return "R_AARCH64_IRELATIVE";
        default: {
            static char buf[32];
            snprintf(buf, sizeof(buf), "UNKNOWN(%u)", type);
            return buf;
        }
    }
}

bool RelocationTable::isPLTReloc(uint32_t type) {
    return type == R_AARCH64_JUMP_SLOT;
}

void RelocationTable::printRelocations() const {
    printf("\nRelocation table (%s):\n", isPLT ? ".rela.plt" : ".rela.dyn");
    printf("  Offset          Info           Type           Sym. Value    Sym. Name\n");

    for (const auto& rel : relocations) {
        const char* symName = rel.symbol ? rel.symbol->name.c_str() : "???";
        uint64_t symValue = rel.symbol ? rel.symbol->value : 0;

        printf("%016lx  %016lx %-14s %016lx %s\n",
               (unsigned long)rel.offset,
               (unsigned long)rel.info,
               getTypeName(rel.type),
               (unsigned long)symValue,
               symName);
    }
}

void RelocationTable::printPLTRelocations() const {
    if (!isPLT) {
        printf("\nNot a PLT relocation table\n");
        return;
    }

    printf("\nPLT relocations (function jumps):\n");
    printf("  Entry  Offset           GOT Index  Symbol\n");

    for (const auto& rel : relocations) {
        // 假设 .got.plt 从 0 开始计算相对索引
        // 实际使用时需要传入 got.plt 的起始地址
        uint32_t gotIndex = 3 + rel.index;  // rela[0] -> GOT[3]

        printf("[%5d] %016lx [%3d]      %s\n",
               rel.index,
               (unsigned long)rel.offset,
               gotIndex,
               rel.symbol ? rel.symbol->name.c_str() : "???");
    }
}