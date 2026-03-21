// elf_dynamic.h - .dynamic 段解析
// 动态链接的核心元数据：依赖库、符号表位置等

#ifndef ELF_DYNAMIC_H
#define ELF_DYNAMIC_H

#include "elf_types.h"
#include <vector>
#include <string>

// ========================================
// 动态段标签常量 (d_tag)
// ========================================
enum ElfDynamicTag : uint64_t {
    DT_NULL         = 0,            // 标记数组结束
    DT_NEEDED       = 1,            // 需要的共享库（字符串表偏移）
    DT_PLTRELSZ     = 2,            // PLT 重定位表大小
    DT_PLTGOT       = 3,            // PLT/GOT 地址
    DT_HASH         = 4,            // 符号哈希表地址
    DT_STRTAB       = 5,            // 动态字符串表地址
    DT_SYMTAB       = 6,            // 动态符号表地址
    DT_RELA         = 7,            // RELA 重定位表地址
    DT_RELASZ       = 8,            // RELA 重定位表大小
    DT_RELAENT      = 9,            // RELA 条目大小
    DT_STRSZ        = 10,           // 字符串表大小
    DT_SYMENT       = 11,           // 符号表条目大小
    DT_INIT         = 12,           // 初始化函数地址
    DT_FINI         = 13,           // 终止函数地址
    DT_SONAME       = 14,           // 共享对象名称（字符串表偏移）
    DT_RPATH        = 15,           // 库搜索路径（已废弃）
    DT_SYMBOLIC     = 16,           // 符号解析标志
    DT_REL          = 17,           // REL 重定位表地址
    DT_RELSZ        = 18,           // REL 重定位表大小
    DT_RELENT       = 19,           // REL 条目大小
    DT_PLTREL       = 20,           // PLT 重定位类型（REL/RELA）
    DT_DEBUG        = 21,           // 调试用
    DT_TEXTREL      = 22,           // 文本段重定位标志
    DT_JMPREL       = 23,           // PLT 重定位表地址（.rela.plt）
    DT_BIND_NOW     = 24,           // 立即绑定标志
    DT_INIT_ARRAY   = 25,           // 构造函数指针数组
    DT_FINI_ARRAY   = 26,           // 析构函数指针数组
    DT_INIT_ARRAYSZ = 27,           // 构造函数数组大小
    DT_FINI_ARRAYSZ = 28,           // 析构函数数组大小
    DT_RUNPATH      = 29,           // 库搜索路径（替代 RPATH）
    DT_FLAGS        = 30,           // 标志位
    DT_ENCODING     = 32,           // 编码信息开始
    DT_PREINIT_ARRAY = 32,          // 预构造函数数组
    DT_PREINIT_ARRAYSZ = 33,        // 预构造函数数组大小
    DT_GNU_HASH     = 0x6ffffef5,   // GNU 哈希表地址
    DT_TLSDESC_PLT  = 0x6ffffef6,   // TLS 描述符 PLT
    DT_TLSDESC_GOT  = 0x6ffffef7,   // TLS 描述符 GOT
    DT_FLAGS_1      = 0x6ffffffb,   // 扩展标志位
    DT_VERDEF       = 0x6ffffffc,   // 版本定义
    DT_VERDEFNUM    = 0x6ffffffd,   // 版本定义数量
    DT_VERNEED      = 0x6ffffffe,   // 版本需求
    DT_VERNEEDNUM   = 0x6fffffff,   // 版本需求数量
    // Android 特定
    DT_ANDROID_REL  = 0x60000001,   // Android 重定位表
    DT_ANDROID_RELSZ = 0x60000002,  // Android 重定位表大小
    DT_ANDROID_RELA = 0x60000003,   // Android RELA 重定位表
    DT_ANDROID_RELASZ = 0x60000004, // Android RELA 重定位表大小
};

// ========================================
// 动态段条目结构 (64-bit)
// ========================================
struct Elf64_Dyn {
    uint64_t d_tag;     // 条目类型
    union {
        uint64_t d_val; // 整数值（字符串表偏移等）
        uint64_t d_ptr; // 地址指针
    } d_un;
};

// ========================================
// 动态段条目信息封装
// ========================================
struct DynamicEntry {
    uint32_t index;     // 在数组中的索引
    uint64_t tag;       // d_tag
    uint64_t value;     // d_un.d_val（整数值）
    uint64_t ptr;       // d_un.d_ptr（地址）

    // 判断 d_un 是值还是指针
    // 根据 ELF 规范，某些 tag 使用 d_val，某些使用 d_ptr
    bool isValue() const {
        switch (tag) {
            case DT_NEEDED:
            case DT_PLTRELSZ:
            case DT_RELASZ:
            case DT_RELAENT:
            case DT_STRSZ:
            case DT_SYMENT:
            case DT_SONAME:
            case DT_RPATH:
            case DT_RELSZ:
            case DT_RELENT:
            case DT_PLTREL:
            case DT_INIT_ARRAYSZ:
            case DT_FINI_ARRAYSZ:
            case DT_RUNPATH:
            case DT_FLAGS:
            case DT_PREINIT_ARRAYSZ:
            case DT_VERDEFNUM:
            case DT_VERNEEDNUM:
                return true;
            default:
                return false;
        }
    }

    bool isPointer() const {
        return tag != DT_NULL && !isValue();
    }

    // 判断是否是字符串偏移类型
    bool isStringOffset() const {
        return tag == DT_NEEDED || tag == DT_SONAME || tag == DT_RPATH || tag == DT_RUNPATH;
    }
};

// ========================================
// 动态段管理类
// ========================================
class DynamicTable {
public:
    std::vector<DynamicEntry> entries;
    std::vector<const DynamicEntry*> neededLibs;  // DT_NEEDED 条目指针列表
    const DynamicEntry* soname = nullptr;           // DT_SONAME 条目
    const DynamicEntry* symtab = nullptr;           // DT_SYMTAB
    const DynamicEntry* strtab = nullptr;           // DT_STRTAB
    const DynamicEntry* hash = nullptr;             // DT_HASH
    const DynamicEntry* gnuHash = nullptr;          // DT_GNU_HASH
    const DynamicEntry* jmprel = nullptr;           // DT_JMPREL (.rela.plt)
    const DynamicEntry* rela = nullptr;             // DT_RELA (.rela.dyn)
    const DynamicEntry* rel = nullptr;              // DT_REL (另一种重定位)
    const DynamicEntry* pltgot = nullptr;           // DT_PLTGOT
    const DynamicEntry* init = nullptr;             // DT_INIT
    const DynamicEntry* fini = nullptr;             // DT_FINI
    const DynamicEntry* initArray = nullptr;        // DT_INIT_ARRAY
    const DynamicEntry* finiArray = nullptr;        // DT_FINI_ARRAY

    // .dynstr 数据指针（用于解析字符串）
    const uint8_t* dynstrData = nullptr;
    size_t dynstrSize = 0;

private:
    bool is64bit_ = false;
    bool isLittleEndian_ = true;

public:
    // 解析 .dynamic 段
    // data: .dynamic section 的数据指针
    // size: .dynamic section 的大小
    // dynstrData: .dynstr section 的数据指针（用于解析字符串）
    // dynstrSize: .dynstr section 的大小
    bool parse(const uint8_t* data, size_t size,
               const uint8_t* dynstrData, size_t dynstrSize,
               bool is64bit, bool isLittleEndian);

    // 获取字符串（从 .dynstr）
    const char* getString(uint64_t offset) const;

    // 打印所有条目
    void print() const;

    // 打印依赖库列表
    void printNeededLibs() const;

    // 获取标签名称
    static const char* getTagName(uint64_t tag);

    // 获取标签描述
    static const char* getTagDescription(uint64_t tag);
};

#endif // ELF_DYNAMIC_H
