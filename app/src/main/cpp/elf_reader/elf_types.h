// elf_types.h - ELF 数据结构定义
// 支持 32/64 位 ELF，小端/大端

#ifndef ELF_TYPES_H
#define ELF_TYPES_H

#include <cstdint>
#include <cstddef>

// ========================================
// 字节序读取辅助函数（inline，供多个文件使用）
// ========================================

template<typename T>
inline T readLE(const uint8_t* p) {
    T val = 0;
    for (size_t i = 0; i < sizeof(T); i++) {
        val |= static_cast<T>(p[i]) << (i * 8);
    }
    return val;
}

template<typename T>
inline T readBE(const uint8_t* p) {
    T val = 0;
    for (size_t i = 0; i < sizeof(T); i++) {
        val |= static_cast<T>(p[i]) << ((sizeof(T) - 1 - i) * 8);
    }
    return val;
}

// 根据 ELF 文件的字节序读取数据
template<typename T>
inline T readVal(const uint8_t* p, bool littleEndian) {
    return littleEndian ? readLE<T>(p) : readBE<T>(p);
}

// ========================================
// ELF 常量定义
// ========================================

// ELF Magic
static constexpr uint8_t ELFMAG[] = {0x7f, 'E', 'L', 'F'};
static constexpr size_t EI_NIDENT = 16;

// ELF 类别
enum ElfClass : uint8_t {
    ELFCLASSNONE = 0,
    ELFCLASS32 = 1,    // 32-bit
    ELFCLASS64 = 2     // 64-bit
};

// 数据编码
enum ElfData : uint8_t {
    ELFDATANONE = 0,
    ELFDATA2LSB = 1,   // 小端
    ELFDATA2MSB = 2    // 大端
};

// 机器架构
enum ElfMachine : uint16_t {
    EM_NONE = 0,
    EM_386 = 3,        // x86
    EM_ARM = 40,       // ARM
    EM_X86_64 = 62,    // x86_64
    EM_AARCH64 = 183   // ARM64
};

// 文件类型
enum ElfType : uint16_t {
    ET_NONE = 0,
    ET_REL = 1,        // 可重定位文件
    ET_EXEC = 2,       // 可执行文件
    ET_DYN = 3,        // 动态库（so）
    ET_CORE = 4        // Core dump
};

// ========================================
// ELF Header (32-bit)
// ========================================
struct Elf32_Ehdr {
    uint8_t  e_ident[EI_NIDENT];  // Magic + 文件信息
    uint16_t e_type;              // 文件类型
    uint16_t e_machine;           // 目标架构
    uint32_t e_version;           // 版本
    uint32_t e_entry;             // 入口点地址
    uint32_t e_phoff;             // Program header 偏移
    uint32_t e_shoff;             // Section header 偏移
    uint32_t e_flags;             // 处理器特定标志
    uint16_t e_ehsize;            // ELF header 大小
    uint16_t e_phentsize;         // Program header 条目大小
    uint16_t e_phnum;             // Program header 条目数量
    uint16_t e_shentsize;         // Section header 条目大小
    uint16_t e_shnum;             // Section header 条目数量
    uint16_t e_shstrndx;          // 字符串表 section 索引
};

// ========================================
// ELF Header (64-bit)
// ========================================
struct Elf64_Ehdr {
    uint8_t  e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

// ========================================
// 通用封装类
// ========================================
class ElfHeader {
public:
    bool is64bit = false;
    bool isLittleEndian = true;

    // e_ident 解析后的字段
    uint8_t ei_class = 0;
    uint8_t ei_data = 0;
    uint8_t ei_version = 0;
    uint8_t ei_osabi = 0;

    // 基本元数据
    uint16_t e_type = 0;
    uint16_t e_machine = 0;
    uint32_t e_version = 0;
    uint64_t e_entry = 0;
    uint32_t e_flags = 0;     // 处理器特定标志

    // Header 位置和大小
    uint64_t e_phoff = 0;     // Program header 文件偏移
    uint64_t e_shoff = 0;     // Section header 文件偏移
    uint16_t e_ehsize = 0;
    uint16_t e_phentsize = 0;
    uint16_t e_phnum = 0;
    uint16_t e_shentsize = 0;
    uint16_t e_shnum = 0;
    uint16_t e_shstrndx = 0;  // Section 名称字符串表索引

public:
    // 从文件缓冲区解析
    bool parse(const uint8_t* data, size_t size);

    // 辅助方法
    const char* getClassName() const;
    const char* getDataName() const;
    const char* getTypeName() const;
    const char* getMachineName() const;
    const char* getOSABIName() const;

    // 打印 Header 信息
    void print() const;
};

#endif // ELF_TYPES_H