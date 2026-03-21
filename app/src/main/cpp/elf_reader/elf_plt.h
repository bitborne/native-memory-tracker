// elf_plt.h - PLT 解析和指令反汇编
// 理解延迟绑定的关键：PLT 代码如何跳转到 GOT

#ifndef ELF_PLT_H
#define ELF_PLT_H

#include "elf_types.h"
#include "elf_sections.h"
#include "elf_relocations.h"
#include <vector>
#include <string>
#include <cstring>

// ========================================
// PLT 条目信息
// ========================================
struct PLTEntry {
    uint32_t index;          // PLT 条目索引 (0 = PLT[0] 总服务台, n = PLT[n] 第n个函数)
    uint64_t fileOffset;     // 在文件中的偏移
    uint64_t virtualAddr;    // 虚拟地址 (运行时地址)
    uint8_t rawBytes[16];    // 原始机器码 (ARM64: 4条指令 = 16字节, x86_64: 可变)

    // 解码后的信息
    bool isResolved;         // 是否已解码
    uint32_t gotIndex;       // 对应的 GOT 索引 (从指令中解析)
    uint64_t gotOffset;      // 到 GOT 的偏移 (从指令中解析)

    // ARM64 特定解码
    struct ARM64Info {
        uint32_t adrpImm;    // adrp 指令的立即数
        uint32_t ldrImm;     // ldr 指令的立即数偏移
        uint32_t addImm;     // add 指令的立即数
        uint64_t targetGOT;  // 计算的目标 GOT 地址
    } arm64;

    // x86_64 特定解码
    struct X86_64Info {
        int32_t jmpOffset;   // jmp 指令的相对偏移
        uint32_t pushIndex;  // push 的重定位索引
    } x86_64;

    PLTEntry() : index(0), fileOffset(0), virtualAddr(0), isResolved(false),
                 gotIndex(0), gotOffset(0) {
        memset(rawBytes, 0, sizeof(rawBytes));
    }
};

// ========================================
// PLT 表管理类
// ========================================
class PLTTable {
public:
    std::vector<PLTEntry> entries;
    uint64_t pltBaseAddr;    // PLT 起始虚拟地址
    uint64_t gotBaseAddr;    // GOT 起始虚拟地址 (从外部传入)

private:
    bool is64bit_ = false;
    bool isLittleEndian_ = true;
    uint32_t machine_ = 0;   // ELF 机器类型 (183=ARM64, 62=X86_64)

public:
    PLTTable() : pltBaseAddr(0), gotBaseAddr(0) {}

    // 解析 PLT section
    // pltData: .plt section 的数据指针
    // pltSize: .plt section 大小
    // pltAddr: .plt 的运行时虚拟地址
    // machine: ELF header 的 e_machine 字段
    bool parse(const uint8_t* pltData, size_t pltSize,
               uint64_t pltAddr, bool is64bit, bool isLittleEndian,
               uint32_t machine);

    // 设置 GOT 基址（用于验证计算）
    void setGOTBase(uint64_t gotAddr) { gotBaseAddr = gotAddr; }

    // 根据重定位表验证 PLT 条目
    // 检查 PLT 计算出的 GOT 索引是否与重定位表一致
    bool verifyAgainstRelocations(const RelocationTable& relocs);

    // 根据索引查找 PLT 条目
    const PLTEntry* findByIndex(uint32_t index) const;

    // 根据 GOT 索引查找对应的 PLT 条目
    const PLTEntry* findByGOTIndex(uint32_t gotIndex) const;

    // 打印所有 PLT 条目
    void print() const;

    // 打印单个 PLT 条目的反汇编
    void printEntry(const PLTEntry& entry) const;

    // 获取架构名称
    static const char* getArchName(uint32_t machine);

private:
    // ARM64 PLT 解析
    bool parseARM64(const uint8_t* pltData, size_t pltSize);
    bool decodeARM64Entry(PLTEntry& entry, const uint8_t* data);
    bool decodeARM64_PLT0(PLTEntry& entry, const uint8_t* data);  // PLT[0] 特殊格式

    // x86_64 PLT 解析
    bool parseX86_64(const uint8_t* pltData, size_t pltSize);
    bool decodeX86_64Entry(PLTEntry& entry, const uint8_t* data, size_t maxSize);
    bool decodeX86_64_PLT0(PLTEntry& entry, const uint8_t* data, size_t maxSize);

    // 辅助函数：从指令编码提取立即数
    uint32_t extractARM64_ADRPImmediate(uint32_t instruction);
    uint32_t extractARM64_LDRImmediate(uint32_t instruction);
    uint32_t extractARM64_ADDImmediate(uint32_t instruction);

    // 打印 ARM64 汇编
    void printARM64Assembly(const PLTEntry& entry) const;

    // 打印 x86_64 汇编
    void printX86_64Assembly(const PLTEntry& entry) const;

    // 读取 32-bit 指令
    static uint32_t readInstruction32(const uint8_t* data, bool littleEndian);
};

#endif // ELF_PLT_H