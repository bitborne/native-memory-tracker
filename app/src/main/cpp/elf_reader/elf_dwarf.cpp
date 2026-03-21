// elf_dwarf.cpp - DWARF .debug_line 解析实现
// 参考：DWARF 4 标准 Section 6.2

#include "elf_dwarf.h"
#include <cstdio>
#include <cstring>

// ========================================
// 字节序读取工具
// ========================================

uint16_t DwarfLineParser::readU16(const uint8_t* data, size_t pos) const {
    if (isLittleEndian_) {
        return (uint16_t)data[pos] | ((uint16_t)data[pos+1] << 8);
    }
    return ((uint16_t)data[pos] << 8) | (uint16_t)data[pos+1];
}

uint32_t DwarfLineParser::readU32(const uint8_t* data, size_t pos) const {
    if (isLittleEndian_) {
        return (uint32_t)data[pos]       | ((uint32_t)data[pos+1] << 8) |
               ((uint32_t)data[pos+2] << 16) | ((uint32_t)data[pos+3] << 24);
    }
    return ((uint32_t)data[pos] << 24) | ((uint32_t)data[pos+1] << 16) |
           ((uint32_t)data[pos+2] << 8)  | (uint32_t)data[pos+3];
}

uint64_t DwarfLineParser::readU64(const uint8_t* data, size_t pos) const {
    if (isLittleEndian_) {
        uint64_t lo = readU32(data, pos);
        uint64_t hi = readU32(data, pos + 4);
        return lo | (hi << 32);
    }
    uint64_t hi = readU32(data, pos);
    uint64_t lo = readU32(data, pos + 4);
    return (hi << 32) | lo;
}

uint64_t DwarfLineParser::readULEB128(const uint8_t* data, size_t size, size_t& pos) {
    uint64_t result = 0;
    uint32_t shift = 0;
    while (pos < size) {
        uint8_t byte = data[pos++];
        result |= ((uint64_t)(byte & 0x7f)) << shift;
        if (!(byte & 0x80)) break;
        shift += 7;
        if (shift >= 64) break;
    }
    return result;
}

int64_t DwarfLineParser::readSLEB128(const uint8_t* data, size_t size, size_t& pos) {
    int64_t result = 0;
    uint32_t shift = 0;
    uint8_t byte = 0;
    while (pos < size) {
        byte = data[pos++];
        result |= ((int64_t)(byte & 0x7f)) << shift;
        shift += 7;
        if (!(byte & 0x80)) break;
        if (shift >= 64) break;
    }
    // 符号扩展
    if (shift < 64 && (byte & 0x40)) {
        result |= -(((int64_t)1) << shift);
    }
    return result;
}

std::string DwarfLineParser::readString(const uint8_t* data, size_t size, size_t& pos) {
    std::string s;
    while (pos < size && data[pos] != '\0') {
        s += (char)data[pos++];
    }
    if (pos < size) pos++;  // 跳过 NULL
    return s;
}

// ========================================
// 主解析入口
// ========================================

bool DwarfLineParser::parse(const uint8_t* data, size_t size,
                             bool is64bit, bool isLittleEndian) {
    is64bit_ = is64bit;
    isLittleEndian_ = isLittleEndian;

    if (!data || size == 0) return false;

    size_t pos = 0;
    while (pos < size) {
        DwarfCompileUnit unit;
        if (!parseUnit(unit, data, size, pos)) {
            break;
        }
        units.push_back(std::move(unit));
    }

    return !units.empty();
}

// ========================================
// 解析单个编译单元
// ========================================

bool DwarfLineParser::parseUnit(DwarfCompileUnit& unit, const uint8_t* data,
                                  size_t size, size_t& pos) {
    size_t unitStart = pos;

    if (pos + 4 > size) return false;

    // 检查是否为 DWARF 64-bit 格式
    uint32_t initialLen = readU32(data, pos);
    pos += 4;

    uint64_t unitLen;
    if (initialLen == 0xffffffff) {
        // DWARF 64-bit
        if (pos + 8 > size) return false;
        unitLen = readU64(data, pos);
        pos += 8;
        unit.header.is64bit_dwarf = true;
    } else {
        unitLen = initialLen;
        unit.header.is64bit_dwarf = false;
    }

    unit.header.unit_length = (uint32_t)unitLen;

    size_t unitEnd = pos + unitLen;
    if (unitEnd > size) {
        // 数据截断，尝试到文件末尾
        unitEnd = size;
    }

    if (!parseHeader(unit.header, data, unitEnd, pos)) {
        pos = unitEnd;
        return false;
    }

    if (!executeProgram(unit, data, unitEnd, pos)) {
        pos = unitEnd;
    }

    pos = unitEnd;
    return true;
}

// ========================================
// 解析头部
// ========================================

bool DwarfLineParser::parseHeader(DwarfLineHeader& header, const uint8_t* data,
                                   size_t size, size_t& pos) {
    if (pos + 2 > size) return false;

    // DWARF 版本
    header.version = readU16(data, pos);
    pos += 2;

    if (header.version < 2 || header.version > 5) {
        // 不支持的版本，跳过
        return false;
    }

    // DWARF 5 在版本之后有 address_size 和 segment_selector_size
    if (header.version >= 5) {
        if (pos + 2 > size) return false;
        pos += 2;  // address_size (1) + segment_selector_size (1)
    }

    // header_length
    if (header.is64bit_dwarf) {
        if (pos + 8 > size) return false;
        header.header_length = readU64(data, pos);
        pos += 8;
    } else {
        if (pos + 4 > size) return false;
        header.header_length = readU32(data, pos);
        pos += 4;
    }

    size_t programStart = pos + header.header_length;

    // 固定字段
    if (pos + 4 > size) return false;
    header.min_insn_length = data[pos++];
    if (header.version >= 4) {
        header.max_ops_per_insn = data[pos++];
    } else {
        header.max_ops_per_insn = 1;
    }
    header.default_is_stmt = data[pos++];
    header.line_base = (int8_t)data[pos++];
    if (pos >= size) return false;
    header.line_range = data[pos++];
    header.opcode_base = data[pos++];

    // 标准操作码长度
    if (header.opcode_base == 0) return false;
    header.standard_opcode_lengths.resize(header.opcode_base - 1);
    for (int i = 0; i < header.opcode_base - 1; i++) {
        if (pos >= size) return false;
        header.standard_opcode_lengths[i] = data[pos++];
    }

    // DWARF 5 使用不同的格式，简化处理：直接跳到程序开始
    if (header.version >= 5) {
        pos = programStart;
        return true;
    }

    // DWARF 4 及更早：目录列表（以两个 NULL 结尾）
    // 第一个条目是编译目录（隐式）
    header.directories.clear();
    while (pos < size) {
        if (data[pos] == '\0') {
            pos++;  // 空列表结束符
            break;
        }
        header.directories.push_back(readString(data, size, pos));
    }

    // 文件列表（以 NULL 条目结尾）
    header.files.clear();
    while (pos < size) {
        if (data[pos] == '\0') {
            pos++;  // 列表结束符
            break;
        }
        DwarfLineHeader::FileEntry entry;
        entry.name = readString(data, size, pos);
        entry.dir_index = (uint32_t)readULEB128(data, size, pos);
        entry.mtime = readULEB128(data, size, pos);
        entry.size = readULEB128(data, size, pos);
        header.files.push_back(std::move(entry));
    }

    pos = programStart;
    return true;
}

// ========================================
// 执行行号状态机程序
// DWARF 4 标准 Section 6.2.5
// ========================================

bool DwarfLineParser::executeProgram(DwarfCompileUnit& unit, const uint8_t* data,
                                      size_t unitEnd, size_t& pos) {
    const DwarfLineHeader& hdr = unit.header;
    DwarfLineMachineState state;
    state.reset(hdr.default_is_stmt != 0);

    auto emitRow = [&]() {
        DwarfLineEntry entry;
        entry.address = state.address;
        entry.file = state.file;
        entry.line = state.line;
        entry.column = state.column;
        entry.is_stmt = state.is_stmt;
        entry.end_sequence = state.end_sequence;
        unit.lines.push_back(entry);
    };

    while (pos < unitEnd) {
        uint8_t opcode = data[pos++];

        if (opcode == 0) {
            // 扩展操作码
            if (pos >= unitEnd) break;
            uint64_t extLen = readULEB128(data, unitEnd, pos);
            if (pos >= unitEnd || extLen == 0) break;
            uint8_t extOpcode = data[pos++];
            size_t extEnd = pos + (extLen - 1);
            if (extEnd > unitEnd) extEnd = unitEnd;

            switch (extOpcode) {
                case 1: { // DW_LNE_end_sequence
                    state.end_sequence = true;
                    emitRow();
                    state.reset(hdr.default_is_stmt != 0);
                    break;
                }
                case 2: { // DW_LNE_set_address
                    if (is64bit_) {
                        if (pos + 8 <= unitEnd) {
                            state.address = readU64(data, pos);
                            pos += 8;
                        }
                    } else {
                        if (pos + 4 <= unitEnd) {
                            state.address = readU32(data, pos);
                            pos += 4;
                        }
                    }
                    state.op_index = 0;
                    break;
                }
                case 3: { // DW_LNE_define_file (deprecated in v5)
                    DwarfLineHeader::FileEntry entry;
                    entry.name = readString(data, unitEnd, pos);
                    entry.dir_index = (uint32_t)readULEB128(data, unitEnd, pos);
                    entry.mtime = readULEB128(data, unitEnd, pos);
                    entry.size = readULEB128(data, unitEnd, pos);
                    // 动态添加文件条目（只在本地用，不追加到 header 以免混乱）
                    break;
                }
                default:
                    break;
            }
            pos = extEnd;

        } else if (opcode < hdr.opcode_base) {
            // 标准操作码
            switch (opcode) {
                case 1: // DW_LNS_copy
                    emitRow();
                    state.discriminator = 0;
                    state.basic_block = false;
                    state.prologue_end = false;
                    state.epilogue_begin = false;
                    break;
                case 2: { // DW_LNS_advance_pc
                    uint64_t adv = readULEB128(data, unitEnd, pos);
                    state.address += adv * hdr.min_insn_length;
                    break;
                }
                case 3: { // DW_LNS_advance_line
                    int64_t adv = readSLEB128(data, unitEnd, pos);
                    state.line = (uint32_t)((int64_t)state.line + adv);
                    break;
                }
                case 4: { // DW_LNS_set_file
                    state.file = (uint32_t)readULEB128(data, unitEnd, pos);
                    break;
                }
                case 5: { // DW_LNS_set_column
                    state.column = (uint32_t)readULEB128(data, unitEnd, pos);
                    break;
                }
                case 6: // DW_LNS_negate_stmt
                    state.is_stmt = !state.is_stmt;
                    break;
                case 7: // DW_LNS_set_basic_block
                    state.basic_block = true;
                    break;
                case 8: { // DW_LNS_const_add_pc
                    uint8_t adj = 255 - hdr.opcode_base;
                    uint32_t addrAdv = (adj / hdr.line_range) * hdr.min_insn_length;
                    state.address += addrAdv;
                    break;
                }
                case 9: { // DW_LNS_fixed_advance_pc
                    if (pos + 2 <= unitEnd) {
                        state.address += readU16(data, pos);
                        pos += 2;
                    }
                    break;
                }
                case 10: // DW_LNS_set_prologue_end
                    state.prologue_end = true;
                    break;
                case 11: // DW_LNS_set_epilogue_begin
                    state.epilogue_begin = true;
                    break;
                case 12: { // DW_LNS_set_isa
                    state.isa = (uint32_t)readULEB128(data, unitEnd, pos);
                    break;
                }
                default: {
                    // 未知标准操作码：跳过参数
                    if (opcode - 1 < (int)hdr.standard_opcode_lengths.size()) {
                        int nargs = hdr.standard_opcode_lengths[opcode - 1];
                        for (int i = 0; i < nargs; i++) {
                            readULEB128(data, unitEnd, pos);
                        }
                    }
                    break;
                }
            }
        } else {
            // 特殊操作码（Special opcodes）
            // 同时推进地址和行号，然后 emit
            uint8_t adj = opcode - hdr.opcode_base;
            int32_t lineAdv = hdr.line_base + (int32_t)(adj % hdr.line_range);
            uint32_t addrAdv = (adj / hdr.line_range) * hdr.min_insn_length;

            state.line = (uint32_t)((int32_t)state.line + lineAdv);
            state.address += addrAdv;

            emitRow();
            state.discriminator = 0;
            state.basic_block = false;
            state.prologue_end = false;
            state.epilogue_begin = false;
        }
    }

    return !unit.lines.empty();
}

// ========================================
// 辅助：获取文件名（含目录）
// ========================================

std::string DwarfLineParser::getFileName(const DwarfLineHeader& header,
                                          uint32_t fileIndex) const {
    if (fileIndex == 0 || fileIndex > header.files.size()) {
        return "(unknown)";
    }
    const auto& f = header.files[fileIndex - 1];
    if (f.dir_index == 0 || f.dir_index > header.directories.size()) {
        return f.name;
    }
    return header.directories[f.dir_index - 1] + "/" + f.name;
}

// ========================================
// 打印行号摘要
// ========================================

void DwarfLineParser::printSummary() const {
    printf("\n.debug_line 调试行号信息:\n");
    printf("  编译单元数量: %zu\n", units.size());
    printf("\n");

    for (size_t ui = 0; ui < units.size(); ui++) {
        const auto& unit = units[ui];
        const auto& hdr = unit.header;

        printf("  编译单元 [%zu]  DWARF v%u\n", ui, hdr.version);

        // 文件列表
        if (!hdr.files.empty()) {
            printf("    源文件列表:\n");
            for (size_t fi = 0; fi < hdr.files.size(); fi++) {
                std::string fullPath = getFileName(hdr, (uint32_t)(fi + 1));
                printf("      [%2zu] %s\n", fi + 1, fullPath.c_str());
            }
        }

        // 行号统计
        uint64_t minAddr = UINT64_MAX, maxAddr = 0;
        uint32_t lineCount = 0;
        for (const auto& entry : unit.lines) {
            if (!entry.end_sequence) {
                if (entry.address < minAddr) minAddr = entry.address;
                if (entry.address > maxAddr) maxAddr = entry.address;
                lineCount++;
            }
        }
        if (lineCount > 0) {
            printf("    地址范围: 0x%lx - 0x%lx  (%u 个行号条目)\n",
                   (unsigned long)minAddr, (unsigned long)maxAddr, lineCount);
        }
        printf("\n");
    }
}

// ========================================
// 打印详细行号表
// ========================================

void DwarfLineParser::print() const {
    printf("\n.debug_line 行号表:\n");

    if (units.empty()) {
        printf("  (无行号信息)\n\n");
        return;
    }

    for (size_t ui = 0; ui < units.size(); ui++) {
        const auto& unit = units[ui];
        const auto& hdr = unit.header;

        printf("\n编译单元 [%zu]  DWARF v%u\n", ui, hdr.version);
        printf("  min_insn_length=%u  line_base=%d  line_range=%u  opcode_base=%u\n",
               hdr.min_insn_length, hdr.line_base, hdr.line_range, hdr.opcode_base);

        // 打印文件列表
        if (!hdr.files.empty()) {
            printf("\n  文件列表:\n");
            printf("  %4s  %s\n", "编号", "文件名");
            printf("  ----  --------\n");
            for (size_t fi = 0; fi < hdr.files.size(); fi++) {
                std::string fullPath = getFileName(hdr, (uint32_t)(fi + 1));
                printf("  %4zu  %s\n", fi + 1, fullPath.c_str());
            }
        }

        // 打印行号表（只显示语句行，最多 100 条）
        printf("\n  行号表 (仅显示前 100 条语句行):\n");
        printf("  %-18s  %-6s  %-6s  %-4s  %s\n",
               "地址", "文件", "行号", "列", "标志");
        printf("  ------------------  ------  ------  ----  -----\n");

        int count = 0;
        for (const auto& entry : unit.lines) {
            if (entry.end_sequence) continue;
            if (!entry.is_stmt) continue;
            if (count >= 100) {
                printf("  ... (共 %zu 条行号条目)\n", unit.lines.size());
                break;
            }

            std::string fileName;
            if (entry.file > 0 && entry.file <= hdr.files.size()) {
                fileName = hdr.files[entry.file - 1].name;
                // 只显示文件名，不含目录
                size_t slashPos = fileName.rfind('/');
                if (slashPos != std::string::npos) {
                    fileName = fileName.substr(slashPos + 1);
                }
            } else {
                fileName = "?";
            }

            // 截断过长的文件名
            if (fileName.size() > 16) {
                fileName = fileName.substr(0, 13) + "...";
            }

            printf("  0x%016lx  %-6u  %-6u  %-4u  %s\n",
                   (unsigned long)entry.address,
                   entry.file, entry.line, entry.column,
                   entry.is_stmt ? "stmt" : "");
            count++;
        }
        printf("\n");
    }
}

// ========================================
// 根据地址查找源码位置
// ========================================

const DwarfLineEntry* DwarfLineParser::findByAddress(uint64_t addr) const {
    const DwarfLineEntry* best = nullptr;

    for (const auto& unit : units) {
        for (const auto& entry : unit.lines) {
            if (entry.end_sequence) continue;
            if (entry.address <= addr) {
                if (!best || entry.address > best->address) {
                    best = &entry;
                }
            }
        }
    }

    return best;
}
