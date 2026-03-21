// elf_symbols.h - 动态符号表解析
// .dynsym 和 .dynstr 的结构定义与解析

#ifndef ELF_SYMBOLS_H
#define ELF_SYMBOLS_H

#include "elf_types.h"
#include "elf_sections.h"
#include <vector>
#include <string>

// ========================================
// 符号绑定类型 (st_info 高4位)
// ========================================
enum ElfSymbolBind : uint8_t {
    STB_LOCAL = 0,   // 局部符号，只在当前 so 可见
    STB_GLOBAL = 1,  // 全局符号，可被其他 so 引用
    STB_WEAK = 2,    // 弱符号，可被同名全局符号覆盖
    STB_LOOS = 10,   // OS 特定
    STB_HIOS = 12,
    STB_LOPROC = 13, // 处理器特定
    STB_HIPROC = 15
};

// ========================================
// 符号类型 (st_info 低4位)
// ========================================
enum ElfSymbolType : uint8_t {
    STT_NOTYPE = 0,   // 无类型
    STT_OBJECT = 1,   // 数据对象（变量）
    STT_FUNC = 2,     // 函数
    STT_SECTION = 3,  // Section 关联
    STT_FILE = 4,     // 文件名
    STT_COMMON = 5,   // 公共数据
    STT_TLS = 6,      // 线程本地存储
    STT_LOOS = 10,
    STT_HIOS = 12,
    STT_LOPROC = 13,
    STT_HIPROC = 15
};

// ========================================
// 特殊 Section 索引 (st_shndx)
// ========================================
enum ElfSpecialSection : uint16_t {
    SHN_UNDEF = 0,      // 未定义（外部符号）
    SHN_LORESERVE = 0xff00,
    SHN_LOPROC = 0xff00,
    SHN_HIPROC = 0xff1f,
    SHN_LOOS = 0xff20,
    SHN_HIOS = 0xff3f,
    SHN_ABS = 0xfff1,   // 绝对地址，不重定位
    SHN_COMMON = 0xfff2, // 公共块
    SHN_XINDEX = 0xffff, // 使用扩展索引
    SHN_HIRESERVE = 0xffff
};

// ========================================
// 符号表条目结构 (64-bit)
// ========================================
struct Elf64_Sym {
    uint32_t st_name;   // 符号名在 .dynstr 中的偏移
    uint8_t  st_info;   // 类型和绑定信息
    uint8_t  st_other;  // 可见性
    uint16_t st_shndx;  // 关联的 Section 索引
    uint64_t st_value;  // 符号值（地址或偏移）
    uint64_t st_size;   // 符号大小（字节）
};

struct Elf32_Sym {
    uint32_t st_name;
    uint32_t st_value;
    uint32_t st_size;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
};

// ========================================
// 符号信息封装
// ========================================
struct SymbolInfo {
    uint32_t index;         // 在符号表中的索引
    std::string name;       // 符号名
    uint8_t bind;           // 绑定类型 (STB_GLOBAL 等)
    uint8_t type;           // 符号类型 (STT_FUNC 等)
    uint8_t other;          // 可见性
    uint16_t shndx;         // Section 索引
    uint64_t value;         // 地址值
    uint64_t size;          // 大小

    // 辅助判断
    bool isFunction() const { return type == STT_FUNC; }
    bool isObject() const { return type == STT_OBJECT; }
    bool isGlobal() const { return bind == STB_GLOBAL; }
    bool isLocal() const { return bind == STB_LOCAL; }
    bool isWeak() const { return bind == STB_WEAK; }
    bool isUndefined() const { return shndx == SHN_UNDEF; }
    bool isAbsolute() const { return shndx == SHN_ABS; }
};

// ========================================
// 动态符号表管理类
// ========================================
class DynamicSymbolTable {
public:
    std::vector<SymbolInfo> symbols;
    const uint8_t* dynstrData = nullptr;  // .dynstr 数据指针
    size_t dynstrSize = 0;                // .dynstr 大小

private:
    bool is64bit_ = false;
    bool isLittleEndian_ = true;

public:
    // 解析动态符号表
    // dynsymData: .dynsym section 的数据指针
    // dynsymSize: .dynsym section 的大小
    // dynstrData: .dynstr section 的数据指针
    // dynstrSize: .dynstr section 的大小
    bool parse(const uint8_t* dynsymData, size_t dynsymSize,
               const uint8_t* dynstrData, size_t dynstrSize,
               bool is64bit, bool isLittleEndian);

    // 根据名称查找符号
    const SymbolInfo* findByName(const char* name) const;

    // 根据索引查找符号
    const SymbolInfo* findByIndex(uint32_t index) const;

    // 获取符号名（从 dynstr）
    const char* getSymbolName(uint32_t nameOffset) const;

    // 打印所有符号（类似 readelf --dyn-syms）
    void printSymbols() const;

    // 打印函数符号（仅 STT_FUNC）
    void printFunctions() const;

    // 打印未定义符号（需要外部解析的）
    void printUndefined() const;

    // 获取符号类型名称
    static const char* getTypeName(uint8_t type);

    // 获取符号绑定名称
    static const char* getBindName(uint8_t bind);

    // 获取 Section 索引名称
    static const char* getShndxName(uint16_t shndx);
};

#endif // ELF_SYMBOLS_H