// elf_rodata.h - .rodata 只读数据解析
// 提取字符串常量、只读数据内容

#ifndef ELF_RODATA_H
#define ELF_RODATA_H

#include "elf_types.h"
#include "elf_sections.h"
#include <vector>
#include <string>

// ========================================
// 字符串条目
// ========================================
struct StringEntry {
    uint64_t offset;           // 在 .rodata 中的偏移
    uint64_t virtualAddr;      // 虚拟地址
    std::string content;       // 字符串内容
    size_t length;             // 长度

    StringEntry() : offset(0), virtualAddr(0), length(0) {}
};

// ========================================
// .rodata 解析器
// ========================================
class RodataParser {
public:
    const uint8_t* data = nullptr;
    size_t size = 0;
    uint64_t virtualAddr = 0;
    std::vector<StringEntry> strings;

private:
    bool isLittleEndian_ = true;

public:
    // 解析 .rodata section
    bool parse(const uint8_t* data, size_t size, uint64_t vaddr, bool isLittleEndian);

    // 提取所有可打印字符串（长度 >= minLen）
    void extractStrings(size_t minLen = 4);

    // 搜索特定字符串
    const StringEntry* findString(const char* pattern) const;

    // 打印所有字符串（简略）
    void printStrings(size_t maxCount = 50) const;

    // 打印十六进制转储
    void printHexDump(uint64_t offset, size_t length) const;

    // 打印指定偏移的内容
    void printAtOffset(uint64_t offset, size_t length) const;

    // 获取虚拟地址对应的文件偏移
    uint64_t vaddrToOffset(uint64_t vaddr) const;

    // 判断字符是否可打印
    static bool isPrintable(char c);

    // 转义字符串用于显示
    static std::string escapeString(const std::string& str);
};

#endif // ELF_RODATA_H
