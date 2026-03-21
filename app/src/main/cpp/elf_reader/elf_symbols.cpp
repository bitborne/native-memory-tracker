// elf_symbols.cpp - 动态符号表解析实现

#include "elf_symbols.h"
#include <cstdio>

// ========================================
// DynamicSymbolTable 实现
// ========================================

bool DynamicSymbolTable::parse(const uint8_t* dynsymData, size_t dynsymSize,
                               const uint8_t* dynstrData, size_t dynstrSize,
                               bool is64bit, bool isLittleEndian) {
    is64bit_ = is64bit;
    isLittleEndian_ = isLittleEndian;
    this->dynstrData = dynstrData;
    this->dynstrSize = dynstrSize;

    if (!dynsymData || dynsymSize == 0) {
        fprintf(stderr, "Error: No dynamic symbol table data\n");
        return false;
    }

    // 计算符号条目数量
    size_t entrySize = is64bit ? sizeof(Elf64_Sym) : sizeof(Elf32_Sym);
    size_t numEntries = dynsymSize / entrySize;

    if (numEntries == 0) {
        fprintf(stderr, "Warning: Empty dynamic symbol table\n");
        return true;
    }

    symbols.reserve(numEntries);

    for (size_t i = 0; i < numEntries; i++) {
        const uint8_t* entry = dynsymData + i * entrySize;

        SymbolInfo sym;
        sym.index = static_cast<uint32_t>(i);

        uint32_t st_name;
        uint8_t  st_info;
        uint8_t  st_other;
        uint16_t st_shndx;
        uint64_t st_value;
        uint64_t st_size;

        if (is64bit) {
            // 64-bit 符号表条目解析
            // Elf64_Sym 布局：
            // offset 0: st_name (4 bytes)
            // offset 4: st_info (1 byte)
            // offset 5: st_other (1 byte)
            // offset 6: st_shndx (2 bytes)
            // offset 8: st_value (8 bytes)
            // offset 16: st_size (8 bytes)
            // 总计：24 bytes
            st_name  = readVal<uint32_t>(entry + 0, isLittleEndian);
            st_info  = readVal<uint8_t>(entry + 4, isLittleEndian);
            st_other = readVal<uint8_t>(entry + 5, isLittleEndian);
            st_shndx = readVal<uint16_t>(entry + 6, isLittleEndian);
            st_value = readVal<uint64_t>(entry + 8, isLittleEndian);
            st_size  = readVal<uint64_t>(entry + 16, isLittleEndian);
        } else {
            // 32-bit 符号表条目解析
            // Elf32_Sym 布局：
            // offset 0: st_name (4 bytes)
            // offset 4: st_value (4 bytes)
            // offset 8: st_size (4 bytes)
            // offset 12: st_info (1 byte)
            // offset 13: st_other (1 byte)
            // offset 14: st_shndx (2 bytes)
            // 总计：16 bytes
            st_name  = readVal<uint32_t>(entry + 0, isLittleEndian);
            st_value = readVal<uint32_t>(entry + 4, isLittleEndian);
            st_size  = readVal<uint32_t>(entry + 8, isLittleEndian);
            st_info  = readVal<uint8_t>(entry + 12, isLittleEndian);
            st_other = readVal<uint8_t>(entry + 13, isLittleEndian);
            st_shndx = readVal<uint16_t>(entry + 14, isLittleEndian);
        }

        // 填充 SymbolInfo (使用简化字段名：value, size, shndx, other)
        sym.value = st_value;
        sym.size  = st_size;
        sym.shndx = st_shndx;
        sym.other = st_other;

        // 解码 st_info
        sym.bind = st_info >> 4;          // 高4位：绑定类型
        sym.type = st_info & 0x0f;        // 低4位：符号类型

        // 解析符号名
        if (st_name < dynstrSize) {
            sym.name = reinterpret_cast<const char*>(dynstrData + st_name);
        } else {
            sym.name = "";  // 名称超出范围
        }

        symbols.push_back(sym);
    }

    return true;
}

const SymbolInfo* DynamicSymbolTable::findByName(const char* name) const {
    for (const auto& sym : symbols) {
        if (sym.name == name) {
            return &sym;
        }
    }
    return nullptr;
}

const SymbolInfo* DynamicSymbolTable::findByIndex(uint32_t index) const {
    if (index < symbols.size()) {
        return &symbols[index];
    }
    return nullptr;
}

const char* DynamicSymbolTable::getSymbolName(uint32_t nameOffset) const {
    if (nameOffset < dynstrSize) {
        return reinterpret_cast<const char*>(dynstrData + nameOffset);
    }
    return nullptr;
}

const char* DynamicSymbolTable::getTypeName(uint8_t type) {
    switch (type) {
        case STT_NOTYPE:  return "NOTYPE";
        case STT_OBJECT:  return "OBJECT";
        case STT_FUNC:    return "FUNC";
        case STT_SECTION: return "SECTION";
        case STT_FILE:    return "FILE";
        case STT_COMMON:  return "COMMON";
        case STT_TLS:     return "TLS";
        default:          return "UNKNOWN";
    }
}

const char* DynamicSymbolTable::getBindName(uint8_t bind) {
    switch (bind) {
        case STB_LOCAL:  return "LOCAL";
        case STB_GLOBAL: return "GLOBAL";
        case STB_WEAK:   return "WEAK";
        default:         return "UNKNOWN";
    }
}

const char* DynamicSymbolTable::getShndxName(uint16_t shndx) {
    switch (shndx) {
        case SHN_UNDEF:   return "UND";
        case SHN_ABS:     return "ABS";
        case SHN_COMMON:  return "COM";
        default: {
            static char buf[32];
            snprintf(buf, sizeof(buf), "%d", shndx);
            return buf;
        }
    }
}

void DynamicSymbolTable::printSymbols() const {
    printf("\nDynamic symbol table (.dynsym):\n");
    printf("   Num:    Value          Size Type    Bind   Vis      Ndx Name\n");

    for (const auto& sym : symbols) {
        printf("%6d: %016lx %5lu %-7s %-6s %-8s %3s %s\n",
               sym.index,
               (unsigned long)sym.value,
               (unsigned long)sym.size,
               getTypeName(sym.type),
               getBindName(sym.bind),
               "DEFAULT",  // visibility 简化处理
               getShndxName(sym.shndx),
               sym.name.c_str());
    }
}

void DynamicSymbolTable::printFunctions() const {
    printf("\nExported/Imported functions:\n");
    printf("   Num:    Value          Size Type    Bind   Vis      Name\n");

    for (const auto& sym : symbols) {
        if (sym.isFunction()) {
            printf("%6d: %016lx %5lu %-7s %-6s %-8s %s\n",
                   sym.index,
                   (unsigned long)sym.value,
                   (unsigned long)sym.size,
                   getTypeName(sym.type),
                   getBindName(sym.bind),
                   "DEFAULT",
                   sym.name.c_str());
        }
    }
}

void DynamicSymbolTable::printUndefined() const {
    printf("\nUndefined symbols (need external resolution):\n");
    printf("   Num:    Size Type    Bind   Vis      Name\n");

    for (const auto& sym : symbols) {
        if (sym.isUndefined() && !sym.name.empty()) {
            printf("%6d: %5lu %-7s %-6s %-8s %s\n",
                   sym.index,
                   (unsigned long)sym.size,
                   getTypeName(sym.type),
                   getBindName(sym.bind),
                   "DEFAULT",
                   sym.name.c_str());
        }
    }
}