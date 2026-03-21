// elf_dwarf.h - DWARF 调试信息解析（简化版）
// 重点：.debug_line 行号表，地址 <-> 源码行号 映射

#ifndef ELF_DWARF_H
#define ELF_DWARF_H

#include "elf_types.h"
#include <vector>
#include <string>

// ========================================
// DWARF 版本常量
// ========================================
// DWARF 2/3/4/5 都支持

// ========================================
// .debug_line 行号程序头
// ========================================
struct DwarfLineHeader {
    uint32_t unit_length;       // 单元长度（不含自身）
    bool is64bit_dwarf = false; // DWARF 64-bit 格式
    uint16_t version;           // DWARF 版本（2-5）
    uint64_t header_length;     // 头部长度
    uint8_t min_insn_length;    // 最小指令长度（字节）
    uint8_t max_ops_per_insn;   // 每条指令最大操作数（v4+）
    uint8_t default_is_stmt;    // 默认是否为语句
    int8_t  line_base;          // 行号偏移基数
    uint8_t line_range;         // 行号范围
    uint8_t opcode_base;        // 特殊操作码起始值
    std::vector<uint8_t> standard_opcode_lengths;  // 标准操作码参数数量

    // 目录表（v4: include_directories）
    std::vector<std::string> directories;

    // 文件表（v4: file_name_entry）
    struct FileEntry {
        std::string name;
        uint32_t dir_index;     // 目录索引（0 = 当前目录）
        uint64_t mtime;         // 修改时间
        uint64_t size;          // 文件大小
    };
    std::vector<FileEntry> files;
};

// ========================================
// 行号状态机寄存器
// ========================================
struct DwarfLineMachineState {
    uint64_t address = 0;           // 程序计数器
    uint32_t op_index = 0;          // 操作索引
    uint32_t file = 1;              // 文件索引（1-based）
    uint32_t line = 1;              // 行号（1-based）
    uint32_t column = 0;            // 列号
    bool is_stmt = true;            // 是否为语句开始
    bool basic_block = false;       // 是否为基本块开始
    bool end_sequence = false;      // 是否为序列结束
    bool prologue_end = false;      // 是否为函数序言结束
    bool epilogue_begin = false;    // 是否为函数尾声开始
    uint32_t isa = 0;               // 指令集架构
    uint32_t discriminator = 0;     // 区分器

    void reset(bool default_is_stmt) {
        address = 0;
        op_index = 0;
        file = 1;
        line = 1;
        column = 0;
        is_stmt = default_is_stmt;
        basic_block = false;
        end_sequence = false;
        prologue_end = false;
        epilogue_begin = false;
        isa = 0;
        discriminator = 0;
    }
};

// ========================================
// 行号表条目（状态机执行后的结果）
// ========================================
struct DwarfLineEntry {
    uint64_t address;       // 机器码地址
    uint32_t file;          // 文件索引
    uint32_t line;          // 源码行号
    uint32_t column;        // 列号
    bool is_stmt;           // 是否为语句
    bool end_sequence;      // 是否为序列结束
};

// ========================================
// 编译单元（一个 .cpp/.c 文件对应一个 CU）
// ========================================
struct DwarfCompileUnit {
    DwarfLineHeader header;
    std::vector<DwarfLineEntry> lines;
};

// ========================================
// .debug_line 解析器
// ========================================
class DwarfLineParser {
public:
    std::vector<DwarfCompileUnit> units;  // 所有编译单元

    // 解析 .debug_line 节
    // data: 节数据
    // size: 节大小
    // is64bit: ELF 是否为 64-bit
    // isLittleEndian: 是否小端序
    bool parse(const uint8_t* data, size_t size,
               bool is64bit, bool isLittleEndian);

    // 打印所有行号信息（类似 readelf --debug-dump=line）
    void print() const;

    // 打印摘要（只显示文件列表和行数统计）
    void printSummary() const;

    // 根据地址查找源码位置
    // 返回最近的行号条目
    const DwarfLineEntry* findByAddress(uint64_t addr) const;

private:
    bool is64bit_ = false;
    bool isLittleEndian_ = true;

    // 解析单个编译单元
    bool parseUnit(DwarfCompileUnit& unit, const uint8_t* data,
                   size_t size, size_t& pos);

    // 解析头部
    bool parseHeader(DwarfLineHeader& header, const uint8_t* data,
                     size_t size, size_t& pos);

    // 执行行号状态机程序
    bool executeProgram(DwarfCompileUnit& unit, const uint8_t* data,
                        size_t unitEnd, size_t& pos);

    // 读取 ULEB128 编码整数
    uint64_t readULEB128(const uint8_t* data, size_t size, size_t& pos);

    // 读取 SLEB128 编码整数
    int64_t readSLEB128(const uint8_t* data, size_t size, size_t& pos);

    // 读取 u16/u32/u64（考虑字节序）
    uint16_t readU16(const uint8_t* data, size_t pos) const;
    uint32_t readU32(const uint8_t* data, size_t pos) const;
    uint64_t readU64(const uint8_t* data, size_t pos) const;

    // 读取以 NULL 结尾的字符串
    std::string readString(const uint8_t* data, size_t size, size_t& pos);

    // 获取文件名（含目录路径）
    std::string getFileName(const DwarfLineHeader& header, uint32_t fileIndex) const;
};

#endif // ELF_DWARF_H
