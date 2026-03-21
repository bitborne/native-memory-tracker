// elf_rodata.cpp - .rodata 只读数据解析实现

#include "elf_rodata.h"
#include <cstdio>
#include <cctype>
#include <iomanip>
#include <sstream>

// ========================================
// RodataParser 实现
// ========================================

bool RodataParser::parse(const uint8_t* data, size_t size, uint64_t vaddr, bool isLittleEndian) {
    this->data = data;
    this->size = size;
    this->virtualAddr = vaddr;
    this->isLittleEndian_ = isLittleEndian;

    if (!data || size == 0) {
        return false;
    }

    return true;
}

bool RodataParser::isPrintable(char c) {
    // 可打印字符：字母、数字、标点、空格
    return (c >= 32 && c < 127) || c == '\t' || c == '\n' || c == '\r';
}

std::string RodataParser::escapeString(const std::string& str) {
    std::ostringstream oss;
    for (char c : str) {
        switch (c) {
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            case '"': oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            default:
                if (isPrintable(c)) {
                    oss << c;
                } else {
                    oss << "\\x" << std::hex << std::setw(2) << std::setfill('0')
                       << (unsigned char)c;
                }
                break;
        }
    }
    return oss.str();
}

void RodataParser::extractStrings(size_t minLen) {
    strings.clear();

    if (!data || size == 0) return;

    size_t i = 0;
    while (i < size) {
        // 跳过不可打印字符
        while (i < size && !isPrintable(data[i])) {
            i++;
        }

        if (i >= size) break;

        // 记录字符串起始
        size_t start = i;
        std::string str;

        // 收集可打印字符
        while (i < size && isPrintable(data[i])) {
            str += (char)data[i];
            i++;
        }

        // 如果长度满足要求，保存
        if (str.length() >= minLen) {
            StringEntry entry;
            entry.offset = start;
            entry.virtualAddr = virtualAddr + start;
            entry.content = str;
            entry.length = str.length();
            strings.push_back(entry);
        }
    }
}

const StringEntry* RodataParser::findString(const char* pattern) const {
    for (const auto& entry : strings) {
        if (entry.content.find(pattern) != std::string::npos) {
            return &entry;
        }
    }
    return nullptr;
}

void RodataParser::printStrings(size_t maxCount) const {
    printf("\n.rodata 字符串列表（长度 >= 4，前 %zu 个）:\n", maxCount);
    printf("  %-8s %-18s %-6s %s\n", "偏移", "虚拟地址", "长度", "内容");
    printf("  %-8s %-18s %-6s %s\n", "--------", "------------------", "------", "--------------------");

    size_t count = 0;
    for (const auto& entry : strings) {
        printf("  0x%06lx 0x%016lx %6zu \"%s\"\n",
               (unsigned long)entry.offset,
               (unsigned long)entry.virtualAddr,
               entry.length,
               escapeString(entry.content).c_str());

        if (++count >= maxCount) {
            printf("  ... 还有 %zu 个字符串\n", strings.size() - maxCount);
            break;
        }
    }

    printf("\n  总计: %zu 个字符串\n", strings.size());
}

void RodataParser::printHexDump(uint64_t offset, size_t length) const {
    if (!data || offset >= size) return;

    size_t end = offset + length;
    if (end > size) end = size;

    printf("\n.rodata 十六进制转储 (0x%06lx - 0x%06lx):\n",
           (unsigned long)offset, (unsigned long)end);
    printf("  偏移     十六进制数据                          ASCII\n");
    printf("  -------- -------------------------------------- ----------------\n");

    for (size_t i = offset; i < end; i += 16) {
        printf("  0x%06lx ", (unsigned long)i);

        // 十六进制
        for (size_t j = 0; j < 16; j++) {
            if (i + j < end) {
                printf("%02x ", data[i + j]);
            } else {
                printf("   ");
            }
        }

        printf(" ");

        // ASCII
        for (size_t j = 0; j < 16 && i + j < end; j++) {
            char c = data[i + j];
            printf("%c", isPrintable(c) ? c : '.');
        }

        printf("\n");
    }
}

void RodataParser::printAtOffset(uint64_t offset, size_t length) const {
    if (!data || offset >= size) {
        printf("偏移超出范围\n");
        return;
    }

    size_t end = offset + length;
    if (end > size) end = size;

    printf(".rodata @ 0x%06lx (VA: 0x%016lx):\n",
           (unsigned long)offset, (unsigned long)(virtualAddr + offset));

    // 尝试作为字符串打印
    std::string str;
    for (size_t i = offset; i < end && data[i] != 0; i++) {
        if (isPrintable(data[i])) {
            str += (char)data[i];
        } else {
            break;
        }
    }

    if (!str.empty()) {
        printf("  字符串: \"%s\"\n", escapeString(str).c_str());
    }

    // 十六进制
    printf("  十六进制: ");
    for (size_t i = offset; i < end && i < offset + 32; i++) {
        printf("%02x ", data[i]);
    }
    if (end - offset > 32) {
        printf("...");
    }
    printf("\n");
}

uint64_t RodataParser::vaddrToOffset(uint64_t vaddr) const {
    if (vaddr >= virtualAddr && vaddr < virtualAddr + size) {
        return vaddr - virtualAddr;
    }
    return 0;
}
