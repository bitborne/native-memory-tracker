// elf_ehframe.cpp - .eh_frame 解析实现
// 简化版：识别 CIE/FDE 结构，打印基本信息

#include "elf_ehframe.h"
#include <cstdio>

// ========================================
// EHFrameParser 实现
// ========================================

bool EHFrameParser::parse(const uint8_t* data, size_t size,
                          uint64_t sectionAddr, bool is64bit, bool isLittleEndian) {
    is64bit_ = is64bit;
    isLittleEndian_ = isLittleEndian;
    sectionAddr_ = sectionAddr;

    if (!data || size == 0) {
        return false;
    }

    size_t pos = 0;

    while (pos < size) {
        // 记录当前条目偏移
        uint64_t entryOffset = pos;

        // 读取长度（32位）
        if (pos + 4 > size) break;
        uint32_t length = readVal<uint32_t>(data + pos, isLittleEndian_);
        pos += 4;

        // 检查是否为 64 位长度（扩展格式）
        if (length == 0xffffffff) {
            // 64 位长度（罕见）
            if (pos + 8 > size) break;
            // 简化处理：不支持 64 位长度
            fprintf(stderr, "Warning: 64-bit length CIE/FDE not supported\n");
            break;
        }

        // 空条目（终止标记）
        if (length == 0) {
            continue;
        }

        // 检查剩余数据
        if (pos + length > size) {
            fprintf(stderr, "Warning: Invalid CIE/FDE length\n");
            break;
        }

        // 读取 CIE ID
        if (pos + 4 > size) break;
        uint32_t cieId = readVal<uint32_t>(data + pos, isLittleEndian_);

        // CIE 的 CIE_id 为 0（32位）或 0（64位简化版）
        // FDE 的 CIE_pointer 指向关联 CIE 的偏移（负值）
        bool isCIE = (cieId == 0);

        if (isCIE) {
            auto cie = std::make_unique<CIEEntry>();
            cie->offset = entryOffset;
            cie->length = length;
            cie->cieId = cieId;

            size_t parsePos = pos + 4;  // 跳过 CIE ID
            if (parseCIE(cie.get(), data, entryOffset + 4 + length, parsePos)) {
                cies.push_back(std::move(cie));
            }
            // 确保跳到下一个条目
            pos = entryOffset + 4 + length;
        } else {
            // FDE：需要找到关联的 CIE
            // cieId 实际上是从当前位置到 CIE 的偏移（负值，以 4 字节为单位）
            // 在 32-bit: cieId * 4 = 偏移量
            uint64_t ciePointer = entryOffset + 4 - cieId;
            const CIEEntry* cie = nullptr;
            for (const auto& c : cies) {
                if (c->offset == ciePointer) {
                    cie = c.get();
                    break;
                }
            }

            auto fde = std::make_unique<FDEEntry>();
            fde->offset = entryOffset;
            fde->length = length;
            fde->cieId = cieId;

            size_t parsePos = pos + 4;  // 跳过 CIE pointer
            if (parseFDE(fde.get(), data, entryOffset + 4 + length, parsePos, cie)) {
                fdes.push_back(std::move(fde));
            }
            // 确保跳到下一个条目
            pos = entryOffset + 4 + length;
        }
    }

    return true;
}

bool EHFrameParser::parseCIE(CIEEntry* cie, const uint8_t* data, size_t size, size_t& pos) {
    // pos 已经指向 CIE ID 之后的位置
    size_t contentStart = pos;

    // 版本号（1 字节）
    if (pos >= size) return false;
    cie->version = data[pos++];

    // Augmentation 字符串（以 null 结尾）
    while (pos < size && data[pos] != 0) {
        cie->augmentation += (char)data[pos++];
    }
    if (pos < size) pos++;  // 跳过 null

    // 根据版本号解析
    if (cie->version == 1) {
        // 版本 1：code alignment (ULEB128), data alignment (SLEB128), return reg (1 byte)
        cie->codeAlignmentFactor = readULEB128(data, size, pos);
        cie->dataAlignmentFactor = readSLEB128(data, size, pos);
        if (pos >= size) return false;
        cie->returnAddressRegister = data[pos++];
    } else if (cie->version == 3 || cie->version == 4) {
        // 版本 3/4（DWARF 3/4）：类似，但 return reg 是 ULEB128
        cie->codeAlignmentFactor = readULEB128(data, size, pos);
        cie->dataAlignmentFactor = readSLEB128(data, size, pos);
        cie->returnAddressRegister = readULEB128(data, size, pos);
    } else {
        fprintf(stderr, "Warning: Unknown CIE version %d\n", cie->version);
        pos = cie->offset + 4 + cie->length;  // 跳过整个 CIE
        return false;
    }

    // 处理 augmentation 数据（如果以 'z' 开头）
    if (!cie->augmentation.empty() && cie->augmentation[0] == 'z') {
        cie->hasAugmentationData = true;
        cie->augmentationLength = readULEB128(data, size, pos);
        size_t augEnd = pos + cie->augmentationLength;

        // 解析 augmentation 字符串中的每个字符
        for (size_t i = 1; i < cie->augmentation.size() && pos < augEnd; i++) {
            char c = cie->augmentation[i];
            switch (c) {
                case 'L':  // LSDA 编码
                    if (pos < size) cie->lsdaEncoding = data[pos++];
                    break;
                case 'R':  // FDE 编码
                    if (pos < size) cie->fdeEncoding = data[pos++];
                    break;
                case 'P':  // Personality 函数
                    if (pos < size) cie->personalityEncoding = data[pos++];
                    cie->personalityFunction = readEncodedAddress(data, size, pos,
                                                          cie->personalityEncoding, 0);
                    cie->hasPersonality = true;
                    break;
                default:
                    // 未知 augmentation，跳过
                    break;
            }
        }

        pos = augEnd;  // 确保位置正确
    }

    // 读取初始指令（直到 CIE 结束）
    size_t contentEnd = cie->offset + 4 + cie->length;
    while (pos < contentEnd) {
        cie->initialInstructions.push_back(data[pos++]);
    }

    return true;
}

bool EHFrameParser::parseFDE(FDEEntry* fde, const uint8_t* data, size_t size, size_t& pos,
                             const CIEEntry* cie) {
    // pos 已经指向 CIE pointer 之后的位置
    fde->cie = cie;

    // 确定编码方式
    uint8_t encoding = cie ? cie->fdeEncoding : 0x1b;  // 默认：PC-relative, 4-byte

    // 读取 PC begin
    fde->pcBegin = readEncodedAddress(data, size, pos, encoding, sectionAddr_);

    // 读取 PC range
    // range 通常是绝对值或相对于 begin 的偏移，这里简化处理为 4 字节
    if (pos + 4 > size) return false;
    fde->pcRange = readVal<uint32_t>(data + pos, isLittleEndian_);
    pos += 4;

    // 读取 LSDA 指针（如果 CIE 有 'L'）
    if (cie && cie->augmentation.find('L') != std::string::npos) {
        fde->lsdaPointer = readEncodedAddress(data, size, pos, cie->lsdaEncoding, sectionAddr_);
    }

    // 读取指令（直到 FDE 结束）
    size_t contentEnd = fde->offset + 4 + fde->length;
    while (pos < contentEnd) {
        fde->instructions.push_back(data[pos++]);
    }

    return true;
}

// 读取 ULEB128（无符号 LEB128）
uint64_t EHFrameParser::readULEB128(const uint8_t* data, size_t size, size_t& pos) {
    uint64_t result = 0;
    int shift = 0;
    while (pos < size) {
        uint8_t byte = data[pos++];
        result |= (uint64_t)(byte & 0x7f) << shift;
        if ((byte & 0x80) == 0) break;
        shift += 7;
    }
    return result;
}

// 读取 SLEB128（有符号 LEB128）
int64_t EHFrameParser::readSLEB128(const uint8_t* data, size_t size, size_t& pos) {
    int64_t result = 0;
    int shift = 0;
    uint8_t byte;
    while (pos < size) {
        byte = data[pos++];
        result |= (int64_t)(byte & 0x7f) << shift;
        shift += 7;
        if ((byte & 0x80) == 0) break;
    }
    // 符号扩展
    if (shift < 64 && (byte & 0x40)) {
        result |= (~0ULL) << shift;
    }
    return result;
}

// 读取编码后的地址
uint64_t EHFrameParser::readEncodedAddress(const uint8_t* data, size_t size, size_t& pos,
                                           uint8_t encoding, uint64_t base) const {
    // 编码格式：低 4 位 = 应用，高 4 位 = 格式
    uint8_t format = encoding & 0x0f;
    uint8_t application = (encoding >> 4) & 0x0f;

    uint64_t value = 0;
    int addrSize = is64bit_ ? 8 : 4;

    switch (format) {
        case 0x01:  // uleb128
            value = readULEB128(data, size, pos);
            break;
        case 0x02:  // udata2
            if (pos + 2 <= size) {
                value = readVal<uint16_t>(data + pos, isLittleEndian_);
                pos += 2;
            }
            break;
        case 0x03:  // udata4
            if (pos + 4 <= size) {
                value = readVal<uint32_t>(data + pos, isLittleEndian_);
                pos += 4;
            }
            break;
        case 0x04:  // udata8
            if (pos + 8 <= size) {
                value = readVal<uint64_t>(data + pos, isLittleEndian_);
                pos += 8;
            }
            break;
        case 0x09:  // sleb128
            value = (uint64_t)readSLEB128(data, size, pos);
            break;
        default:    // 其他：假设为机器字长
            if (pos + addrSize <= size) {
                if (addrSize == 8) {
                    value = readVal<uint64_t>(data + pos, isLittleEndian_);
                } else {
                    value = readVal<uint32_t>(data + pos, isLittleEndian_);
                }
                pos += addrSize;
            }
            break;
    }

    // 应用修饰（简化处理）
    switch (application) {
        case 0x1:  // P - PC-relative
            value += base + pos;
            break;
        case 0x2:  // T - 文本段相对（忽略）
        case 0x3:  // G - GOT-relative（忽略）
        default:
            break;
    }

    return value;
}

const char* EHFrameParser::getEncodingName(uint8_t encoding) {
    uint8_t format = encoding & 0x0f;
    switch (format) {
        case 0x00: return "abs ptr";
        case 0x01: return "uleb128";
        case 0x02: return "udata2";
        case 0x03: return "udata4";
        case 0x04: return "udata8";
        case 0x09: return "sleb128";
        default: return "unknown";
    }
}

const FDEEntry* EHFrameParser::findFDEByPC(uint64_t pc) const {
    for (const auto& fde : fdes) {
        if (pc >= fde->pcBegin && pc < fde->pcBegin + fde->pcRange) {
            return fde.get();
        }
    }
    return nullptr;
}

void EHFrameParser::printSummary() const {
    printf("\n.eh_frame 异常处理帧摘要:\n");
    printf("  CIE 数量: %zu\n", cies.size());
    printf("  FDE 数量: %zu (函数数量)\n", fdes.size());

    if (!cies.empty()) {
        printf("\n  CIE 信息:\n");
        for (const auto& cie : cies) {
            printf("    CIE@%#06lx: 版本=%d 扩展=\"%s\"\n",
                   (unsigned long)cie->offset,
                   cie->version,
                   cie->augmentation.c_str());
            if (cie->hasAugmentationData) {
                printf("      代码对齐=%lu 数据对齐=%ld\n",
                       (unsigned long)cie->codeAlignmentFactor,
                       (long)cie->dataAlignmentFactor);
                if (cie->hasPersonality) {
                    printf("      Personality 函数: %#lx\n",
                           (unsigned long)cie->personalityFunction);
                }
            }
        }
    }
}

void EHFrameParser::print() const {
    printSummary();

    if (!fdes.empty()) {
        printf("\n  FDE 列表（前 10 个）:\n");
        printf("    %-6s %-18s %-10s %-10s\n", "序号", "PC 起始地址", "范围", "LSDA");
        printf("    %-6s %-18s %-10s %-10s\n", "------", "------------------", "----------", "----------");

        size_t count = 0;
        for (const auto& fde : fdes) {
            printf("    [%4zu] %#016lx %#8lx ",
                   count,
                   (unsigned long)fde->pcBegin,
                   (unsigned long)fde->pcRange);
            if (fde->lsdaPointer) {
                printf("%#lx", (unsigned long)fde->lsdaPointer);
            } else {
                printf("-");
            }
            printf("\n");

            if (++count >= 10) {
                printf("    ... 还有 %zu 个 FDE\n", fdes.size() - 10);
                break;
            }
        }
    }
}

void EHFrameParser::printFDE(const FDEEntry* fde) const {
    if (!fde) return;

    printf("  FDE@%#06lx:\n", (unsigned long)fde->offset);
    printf("    PC 范围: %#lx - %#lx (大小: %lu)\n",
           (unsigned long)fde->pcBegin,
           (unsigned long)(fde->pcBegin + fde->pcRange),
           (unsigned long)fde->pcRange);
    if (fde->cie) {
        printf("    关联 CIE: CIE@%#06lx\n", (unsigned long)fde->cie->offset);
    }
    if (fde->lsdaPointer) {
        printf("    LSDA 指针: %#lx (C++ 异常表)\n", (unsigned long)fde->lsdaPointer);
    }
    printf("    指令字节数: %zu\n", fde->instructions.size());
}
