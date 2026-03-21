// elf_dynamic.cpp - .dynamic 段解析实现

#include "elf_dynamic.h"
#include <cstdio>
#include <cstdint>

// ========================================
// DynamicTable 实现
// ========================================

bool DynamicTable::parse(const uint8_t* data, size_t size,
                         const uint8_t* dynstrData, size_t dynstrSize,
                         bool is64bit, bool isLittleEndian) {
    is64bit_ = is64bit;
    isLittleEndian_ = isLittleEndian;
    this->dynstrData = dynstrData;
    this->dynstrSize = dynstrSize;

    if (!data || size == 0) {
        fprintf(stderr, "Error: No dynamic section data\n");
        return false;
    }

    // 计算条目数量
    size_t entrySize = is64bit ? sizeof(Elf64_Dyn) : 8;  // 32-bit: 8 bytes
    size_t numEntries = size / entrySize;

    entries.reserve(numEntries);

    for (size_t i = 0; i < numEntries; i++) {
        const uint8_t* entry = data + i * entrySize;

        DynamicEntry dyn;
        dyn.index = static_cast<uint32_t>(i);

        if (is64bit) {
            // Elf64_Dyn 布局：
            // offset 0: d_tag (8 bytes)
            // offset 8: d_un (8 bytes)
            dyn.tag = readVal<uint64_t>(entry + 0, isLittleEndian);
            uint64_t d_un = readVal<uint64_t>(entry + 8, isLittleEndian);
            dyn.value = d_un;
            dyn.ptr = d_un;
        } else {
            // Elf32_Dyn 布局：
            // offset 0: d_tag (4 bytes)
            // offset 4: d_un (4 bytes)
            dyn.tag = readVal<uint32_t>(entry + 0, isLittleEndian);
            uint32_t d_un = readVal<uint32_t>(entry + 4, isLittleEndian);
            dyn.value = d_un;
            dyn.ptr = d_un;
        }

        // 分类存储
        switch (dyn.tag) {
            case DT_NEEDED:
                neededLibs.push_back(&entries.emplace_back(dyn));
                break;
            case DT_SONAME:
                soname = &entries.emplace_back(dyn);
                break;
            case DT_SYMTAB:
                symtab = &entries.emplace_back(dyn);
                break;
            case DT_STRTAB:
                strtab = &entries.emplace_back(dyn);
                break;
            case DT_HASH:
                hash = &entries.emplace_back(dyn);
                break;
            case DT_GNU_HASH:
                gnuHash = &entries.emplace_back(dyn);
                break;
            case DT_JMPREL:
                jmprel = &entries.emplace_back(dyn);
                break;
            case DT_RELA:
                rela = &entries.emplace_back(dyn);
                break;
            case DT_REL:
                rel = &entries.emplace_back(dyn);
                break;
            case DT_PLTGOT:
                pltgot = &entries.emplace_back(dyn);
                break;
            case DT_INIT:
                init = &entries.emplace_back(dyn);
                break;
            case DT_FINI:
                fini = &entries.emplace_back(dyn);
                break;
            case DT_INIT_ARRAY:
                initArray = &entries.emplace_back(dyn);
                break;
            case DT_FINI_ARRAY:
                finiArray = &entries.emplace_back(dyn);
                break;
            case DT_NULL:
                // 结束标记，保存但不继续
                entries.emplace_back(dyn);
                return true;
            default:
                entries.emplace_back(dyn);
                break;
        }
    }

    return true;
}

const char* DynamicTable::getString(uint64_t offset) const {
    if (offset < dynstrSize && dynstrData) {
        return reinterpret_cast<const char*>(dynstrData + offset);
    }
    return nullptr;
}

const char* DynamicTable::getTagName(uint64_t tag) {
    switch (tag) {
        case DT_NULL: return "DT_NULL";
        case DT_NEEDED: return "DT_NEEDED";
        case DT_PLTRELSZ: return "DT_PLTRELSZ";
        case DT_PLTGOT: return "DT_PLTGOT";
        case DT_HASH: return "DT_HASH";
        case DT_STRTAB: return "DT_STRTAB";
        case DT_SYMTAB: return "DT_SYMTAB";
        case DT_RELA: return "DT_RELA";
        case DT_RELASZ: return "DT_RELASZ";
        case DT_RELAENT: return "DT_RELAENT";
        case DT_STRSZ: return "DT_STRSZ";
        case DT_SYMENT: return "DT_SYMENT";
        case DT_INIT: return "DT_INIT";
        case DT_FINI: return "DT_FINI";
        case DT_SONAME: return "DT_SONAME";
        case DT_RPATH: return "DT_RPATH";
        case DT_SYMBOLIC: return "DT_SYMBOLIC";
        case DT_REL: return "DT_REL";
        case DT_RELSZ: return "DT_RELSZ";
        case DT_RELENT: return "DT_RELENT";
        case DT_PLTREL: return "DT_PLTREL";
        case DT_DEBUG: return "DT_DEBUG";
        case DT_TEXTREL: return "DT_TEXTREL";
        case DT_JMPREL: return "DT_JMPREL";
        case DT_BIND_NOW: return "DT_BIND_NOW";
        case DT_INIT_ARRAY: return "DT_INIT_ARRAY";
        case DT_FINI_ARRAY: return "DT_FINI_ARRAY";
        case DT_INIT_ARRAYSZ: return "DT_INIT_ARRAYSZ";
        case DT_FINI_ARRAYSZ: return "DT_FINI_ARRAYSZ";
        case DT_RUNPATH: return "DT_RUNPATH";
        case DT_FLAGS: return "DT_FLAGS";
        case DT_GNU_HASH: return "DT_GNU_HASH";
        case DT_TLSDESC_PLT: return "DT_TLSDESC_PLT";
        case DT_TLSDESC_GOT: return "DT_TLSDESC_GOT";
        case DT_FLAGS_1: return "DT_FLAGS_1";
        case DT_VERDEF: return "DT_VERDEF";
        case DT_VERDEFNUM: return "DT_VERDEFNUM";
        case DT_VERNEED: return "DT_VERNEED";
        case DT_VERNEEDNUM: return "DT_VERNEEDNUM";
        case DT_ANDROID_REL: return "DT_ANDROID_REL";
        case DT_ANDROID_RELSZ: return "DT_ANDROID_RELSZ";
        case DT_ANDROID_RELA: return "DT_ANDROID_RELA";
        case DT_ANDROID_RELASZ: return "DT_ANDROID_RELASZ";
        default: {
            static char buf[32];
            if (tag >= 0x60000000 && tag <= 0x6fffffff) {
                snprintf(buf, sizeof(buf), "DT_ANDROID(0x%lx)", (unsigned long)tag);
            } else {
                snprintf(buf, sizeof(buf), "DT_UNKNOWN(0x%lx)", (unsigned long)tag);
            }
            return buf;
        }
    }
}

const char* DynamicTable::getTagDescription(uint64_t tag) {
    switch (tag) {
        case DT_NULL: return "标记数组结束";
        case DT_NEEDED: return "依赖的共享库";
        case DT_PLTRELSZ: return "PLT 重定位表大小";
        case DT_PLTGOT: return "PLT/GOT 地址";
        case DT_HASH: return "符号哈希表地址";
        case DT_STRTAB: return "动态字符串表地址";
        case DT_SYMTAB: return "动态符号表地址";
        case DT_RELA: return "RELA 重定位表地址";
        case DT_RELASZ: return "RELA 重定位表大小";
        case DT_RELAENT: return "RELA 条目大小";
        case DT_STRSZ: return "字符串表大小";
        case DT_SYMENT: return "符号表条目大小";
        case DT_INIT: return "初始化函数地址";
        case DT_FINI: return "终止函数地址";
        case DT_SONAME: return "共享对象名称";
        case DT_RPATH: return "库搜索路径(已废弃)";
        case DT_SYMBOLIC: return "符号解析标志";
        case DT_REL: return "REL 重定位表地址";
        case DT_RELSZ: return "REL 重定位表大小";
        case DT_RELENT: return "REL 条目大小";
        case DT_PLTREL: return "PLT 重定位类型";
        case DT_DEBUG: return "调试用";
        case DT_TEXTREL: return "文本段重定位标志";
        case DT_JMPREL: return "PLT 重定位表地址(.rela.plt)";
        case DT_BIND_NOW: return "立即绑定标志";
        case DT_INIT_ARRAY: return "构造函数指针数组";
        case DT_FINI_ARRAY: return "析构函数指针数组";
        case DT_INIT_ARRAYSZ: return "构造函数数组大小";
        case DT_FINI_ARRAYSZ: return "析构函数数组大小";
        case DT_RUNPATH: return "库搜索路径";
        case DT_FLAGS: return "标志位";
        case DT_GNU_HASH: return "GNU 哈希表地址";
        default: return "";
    }
}

void DynamicTable::print() const {
    printf("\nDynamic Section (.dynamic):\n");
    printf("  Tag                Value/Pointer        Description\n");
    printf("  ------------------ -------------------- --------------------------------\n");

    for (const auto& entry : entries) {
        if (entry.tag == DT_NULL) break;

        const char* tagName = getTagName(entry.tag);
        const char* desc = getTagDescription(entry.tag);

        if (entry.isStringOffset() && dynstrData) {
            // 字符串类型：显示字符串值
            const char* str = getString(entry.value);
            printf("  %-18s 0x%016lx %s\n",
                   tagName, (unsigned long)entry.value,
                   str ? str : desc);
        } else if (entry.isValue()) {
            // 数值类型
            printf("  %-18s %20lu %s\n",
                   tagName, (unsigned long)entry.value, desc);
        } else {
            // 指针类型
            printf("  %-18s 0x%016lx %s\n",
                   tagName, (unsigned long)entry.ptr, desc);
        }
    }
}

void DynamicTable::printNeededLibs() const {
    printf("\n依赖库列表 (DT_NEEDED):\n");

    if (neededLibs.empty()) {
        printf("  [此文件没有依赖其他共享库]\n");
        return;
    }

    for (const auto* entry : neededLibs) {
        const char* libName = getString(entry->value);
        printf("  %s\n", libName ? libName : "[invalid string offset]");
    }

    // 同时显示 SONAME
    if (soname) {
        const char* name = getString(soname->value);
        printf("\nSONAME: %s\n", name ? name : "[none]");
    }
}
