// elf_plt.cpp - PLT 解析和指令反汇编实现

#include "elf_plt.h"
#include <cstdio>
#include <cstdint>

// ========================================
// PLTTable 实现
// ========================================

bool PLTTable::parse(const uint8_t* pltData, size_t pltSize,
                     uint64_t pltAddr, bool is64bit, bool isLittleEndian,
                     uint32_t machine) {
    is64bit_ = is64bit;
    isLittleEndian_ = isLittleEndian;
    machine_ = machine;
    pltBaseAddr = pltAddr;

    if (!pltData || pltSize == 0) {
        fprintf(stderr, "Error: No PLT section data\n");
        return false;
    }

    // 根据架构解析
    switch (machine) {
        case 183: // EM_AARCH64
            return parseARM64(pltData, pltSize);
        case 62: // EM_X86_64
            return parseX86_64(pltData, pltSize);
        default:
            fprintf(stderr, "Warning: Unsupported architecture %u for PLT parsing\n", machine);
            return false;
    }
}

// ========================================
// ARM64 PLT 解析
// ========================================

// ARM64 PLT 条目格式（每个 16 字节 = 4 条 32-bit 指令）：
// PLT[0] - 总服务台（特殊）：
//   stp  x16, x30, [sp, #-16]!   // 保存寄存器
//   adr  x16, #offset            // 加载 GOT[2] 地址
//   ldr  x17, [x16]              // 加载 _dl_runtime_resolve
//   br   x17                     // 跳转
//
// PLT[n] - 普通条目（n >= 1）：
//   adrp x16, #page              // 计算 GOT 页基址
//   ldr  x17, [x16, #off]        // 从 GOT[n+2] 加载地址
//   add  x16, x16, #off          // x16 = &GOT[n+2]
//   br   x17                     // 跳转

bool PLTTable::parseARM64(const uint8_t* pltData, size_t pltSize) {
    // ARM64 PLT 条目固定 16 字节
    const size_t entrySize = 16;

    if (pltSize < entrySize) {
        fprintf(stderr, "Error: PLT section too small for ARM64\n");
        return false;
    }

    size_t numEntries = pltSize / entrySize;

    entries.reserve(numEntries);

    for (size_t i = 0; i < numEntries; i++) {
        const uint8_t* entryData = pltData + i * entrySize;
        uint64_t entryAddr = pltBaseAddr + i * entrySize;

        PLTEntry entry;
        entry.index = static_cast<uint32_t>(i);
        entry.fileOffset = i * entrySize;
        entry.virtualAddr = entryAddr;
        memcpy(entry.rawBytes, entryData, 16);

        if (i == 0) {
            // PLT[0] 是总服务台，特殊处理
            decodeARM64_PLT0(entry, entryData);
        } else {
            // 普通 PLT 条目
            decodeARM64Entry(entry, entryData);
        }

        entries.push_back(entry);
    }

    return true;
}

bool PLTTable::decodeARM64Entry(PLTEntry& entry, const uint8_t* data) {
    // 读取 4 条指令
    uint32_t instr[4];
    for (int i = 0; i < 4; i++) {
        instr[i] = readInstruction32(data + i * 4, isLittleEndian_);
    }

    // 解析指令编码
    // adrp: 1xx1 0000 ... (最高位为 1, bit 30 = 0, bits 31,29 = 10)
    // 格式：adrp Xd, label
    // 编码：1-immlo-1-immhi-Xd

    // ldr (unsigned immediate): 1x11 1001 ...
    // 格式：ldr Xt, [Xn, #pimm]

    // add (immediate): 1001 0001 ...
    // 格式：add Xd, Xn, #imm

    // br: 1101 0110 0001 1111 0000 00nn nnn 00000
    // 格式：br Xn

    // 验证指令模式
    bool isAdrp = ((instr[0] >> 31) == 1) && (((instr[0] >> 29) & 0x3) == 2) && (((instr[0] >> 24) & 0x1F) == 0x10);
    bool isLdr  = ((instr[1] >> 31) == 1) && (((instr[1] >> 22) & 0x3) == 3) && (((instr[1] >> 24) & 0x3F) == 0x39);
    bool isAdd  = ((instr[2] >> 24) & 0xFF) == 0x91;
    bool isBr   = (instr[3] == 0xD61F0000) || ((instr[3] & 0xFFFFFC00) == 0xD61F0000);

    if (!isAdrp || !isLdr || !isAdd || !isBr) {
        // 可能是不同的 PLT 格式，标记为未解析
        entry.isResolved = false;
        return false;
    }

    entry.isResolved = true;

    // 提取 adrp 立即数
    // adrp: bits [30:29] = immlo, bits [23:5] = immhi
    uint64_t immlo = (instr[0] >> 29) & 0x3;
    uint64_t immhi = (instr[0] >> 5) & 0x7FFFF;
    int64_t adrpImm = ((immhi << 2) | immlo) << 12;
    // 符号扩展
    if (adrpImm & 0x10000000000LL) { // 如果第 32 位为 1
        adrpImm |= 0xFFFFFFFE00000000LL; // 符号扩展到 64 位
    }

    // 提取 ldr 立即数偏移
    // ldr: bits [21:10] = imm12
    uint32_t ldrImm = (instr[1] >> 10) & 0xFFF;
    uint32_t ldrOffset = ldrImm * 8; // 64-bit 加载，偏移 = imm12 * 8

    // 提取 add 立即数
    // add: bits [21:10] = imm12
    uint32_t addImm = (instr[2] >> 10) & 0xFFF;

    entry.arm64.adrpImm = static_cast<uint32_t>(adrpImm);
    entry.arm64.ldrImm = ldrOffset;
    entry.arm64.addImm = addImm;

    // 计算目标 GOT 地址
    // adrp 计算页基址，然后加上 ldr/add 的偏移
    uint64_t pageBase = (entry.virtualAddr & ~0xFFFLL) + adrpImm;
    entry.arm64.targetGOT = pageBase + ldrOffset;

    // 计算 GOT 索引
    if (gotBaseAddr != 0) {
        entry.gotOffset = entry.arm64.targetGOT - gotBaseAddr;
        entry.gotIndex = static_cast<uint32_t>(entry.gotOffset / 8);
    }

    return true;
}

bool PLTTable::decodeARM64_PLT0(PLTEntry& entry, const uint8_t* data) {
    // PLT[0] 是总服务台，格式不固定，我们只记录原始字节
    entry.isResolved = false; // 标记为特殊条目
    entry.gotIndex = 2; // PLT[0] 通常使用 GOT[2] (_dl_runtime_resolve)
    return true;
}

// ========================================
// x86_64 PLT 解析
// ========================================

// x86_64 PLT 条目格式：
// PLT[0] - 总服务台：
//   pushq  GOT[1](%rip)          // 压入 link_map
//   jmp    *GOT[2](%rip)         // 跳转到 _dl_runtime_resolve
//   nop
//
// PLT[n] - 普通条目：
//   jmp    *GOT[n+3](%rip)       // 尝试直接跳转
//   pushq  $n                    // 压入重定位索引
//   jmp    PLT[0]                // 跳转到总服务台

bool PLTTable::parseX86_64(const uint8_t* pltData, size_t pltSize) {
    // x86_64 PLT 条目固定 16 字节
    const size_t entrySize = 16;

    if (pltSize < entrySize) {
        fprintf(stderr, "Error: PLT section too small for x86_64\n");
        return false;
    }

    size_t numEntries = pltSize / entrySize;

    entries.reserve(numEntries);

    for (size_t i = 0; i < numEntries; i++) {
        const uint8_t* entryData = pltData + i * entrySize;
        uint64_t entryAddr = pltBaseAddr + i * entrySize;

        PLTEntry entry;
        entry.index = static_cast<uint32_t>(i);
        entry.fileOffset = i * entrySize;
        entry.virtualAddr = entryAddr;
        memcpy(entry.rawBytes, entryData, 16);

        if (i == 0) {
            decodeX86_64_PLT0(entry, entryData, entrySize);
        } else {
            decodeX86_64Entry(entry, entryData, entrySize);
        }

        entries.push_back(entry);
    }

    return true;
}

bool PLTTable::decodeX86_64Entry(PLTEntry& entry, const uint8_t* data, size_t maxSize) {
    if (maxSize < 16) return false;

    // 检查标准 PLT 条目模式
    // jmp *offset(%rip): FF 25 xx xx xx xx  (6 bytes)
    // push $n:           68 xx xx xx xx      (5 bytes)
    // jmp PLT[0]:        E9 xx xx xx xx      (5 bytes)

    bool isJmpIndirect = (data[0] == 0xFF) && (data[1] == 0x25);
    bool isPushImm = (data[6] == 0x68);
    bool isJmpRelative = (data[11] == 0xE9);

    if (!isJmpIndirect || !isPushImm || !isJmpRelative) {
        entry.isResolved = false;
        return false;
    }

    entry.isResolved = true;

    // 解析 jmp *offset(%rip)
    // FF 25 followed by 32-bit signed offset from next instruction
    int32_t jmpOffset;
    memcpy(&jmpOffset, data + 2, 4);
    if (!isLittleEndian_) {
        // 转换字节序
        jmpOffset = __builtin_bswap32(jmpOffset);
    }

    // RIP 相对寻址：jmp 指令结束地址 + offset = GOT 条目地址
    // jmp 指令在 PLT+0 到 PLT+5 (6 字节)，结束地址是 PLT+6
    uint64_t afterJmp = entry.virtualAddr + 6;
    uint64_t gotEntryAddr = afterJmp + jmpOffset;

    entry.x86_64.jmpOffset = jmpOffset;

    // 解析 push $n (重定位索引)
    uint32_t pushIndex;
    memcpy(&pushIndex, data + 7, 4);
    if (!isLittleEndian_) {
        pushIndex = __builtin_bswap32(pushIndex);
    }
    entry.x86_64.pushIndex = pushIndex;

    // 计算 GOT 索引
    if (gotBaseAddr != 0) {
        entry.gotOffset = gotEntryAddr - gotBaseAddr;
        entry.gotIndex = static_cast<uint32_t>(entry.gotOffset / 8);
    }

    return true;
}

bool PLTTable::decodeX86_64_PLT0(PLTEntry& entry, const uint8_t* data, size_t maxSize) {
    // PLT[0] 总服务台
    // push GOT[1](%rip)
    // jmp  *GOT[2](%rip)
    // nop
    entry.isResolved = false;
    entry.gotIndex = 2; // 使用 GOT[2]
    return true;
}

// ========================================
// 辅助函数
// ========================================

uint32_t PLTTable::readInstruction32(const uint8_t* data, bool littleEndian) {
    uint32_t val;
    memcpy(&val, data, 4);
    if (!littleEndian) {
        val = __builtin_bswap32(val);
    }
    return val;
}

const char* PLTTable::getArchName(uint32_t machine) {
    switch (machine) {
        case 183: return "AArch64 (ARM64)";
        case 62:  return "x86-64";
        case 40:  return "ARM";
        case 3:   return "x86";
        default:  return "Unknown";
    }
}

const PLTEntry* PLTTable::findByIndex(uint32_t index) const {
    if (index < entries.size()) {
        return &entries[index];
    }
    return nullptr;
}

const PLTEntry* PLTTable::findByGOTIndex(uint32_t gotIndex) const {
    for (const auto& entry : entries) {
        if (entry.gotIndex == gotIndex) {
            return &entry;
        }
    }
    return nullptr;
}

bool PLTTable::verifyAgainstRelocations(const RelocationTable& relocs) {
    bool allMatch = true;

    printf("\nPLT-GOT 一致性验证:\n");
    printf("  PLT Index | GOT Index (PLT) | GOT Index (Reloc) | Symbol | Match\n");
    printf("  ----------|-----------------|-------------------|--------|------\n");

    // rela[n] 对应 GOT[n+3]
    for (const auto& rel : relocs.relocations) {
        uint32_t expectedGotIndex = 3 + rel.index;
        const PLTEntry* pltEntry = findByGOTIndex(expectedGotIndex);

        if (pltEntry) {
            bool match = (pltEntry->gotIndex == expectedGotIndex);
            const char* symbolName = rel.symbol ? rel.symbol->name.c_str() : "???";
            printf("  %9u | %15u | %17u | %-20s | %s\n",
                   pltEntry->index,
                   pltEntry->gotIndex,
                   expectedGotIndex,
                   symbolName,
                   match ? "✓" : "✗");
            if (!match) allMatch = false;
        }
    }

    return allMatch;
}

// ========================================
// 打印函数
// ========================================

void PLTTable::print() const {
    printf("\nPLT (Procedure Linkage Table) 反汇编:\n");
    printf("  架构: %s\n", getArchName(machine_));
    printf("  PLT 基址: 0x%016lx\n", (unsigned long)pltBaseAddr);
    if (gotBaseAddr != 0) {
        printf("  GOT 基址: 0x%016lx\n", (unsigned long)gotBaseAddr);
    }
    printf("  条目数: %zu\n", entries.size());
    printf("\n");

    for (const auto& entry : entries) {
        printEntry(entry);
    }
}

void PLTTable::printEntry(const PLTEntry& entry) const {
    printf("  PLT[%u] @ 0x%016lx:\n",
           entry.index, (unsigned long)entry.virtualAddr);

    // 打印原始字节
    printf("    Raw: ");
    for (int i = 0; i < 16; i++) {
        printf("%02x ", entry.rawBytes[i]);
    }
    printf("\n");

    if (!entry.isResolved) {
        if (entry.index == 0) {
            printf("    [PLT[0] 总服务台 - 特殊格式]\n");
        } else {
            printf("    [未能识别 PLT 格式]\n");
        }
    } else {
        // 根据架构打印汇编
        if (machine_ == 183 || machine_ == 0xB7) {
            printARM64Assembly(entry);
        } else if (machine_ == 62) {
            printX86_64Assembly(entry);
        }
    }

    printf("\n");
}

void PLTTable::printARM64Assembly(const PLTEntry& entry) const {
    if (entry.index == 0) {
        printf("    stp  x16, x30, [sp, #-16]!  ; 保存寄存器\n");
        printf("    adr  x16, #...              ; 加载 GOT[2] 地址\n");
        printf("    ldr  x17, [x16]             ; 加载 _dl_runtime_resolve\n");
        printf("    br   x17                    ; 跳转解析\n");
    } else {
        printf("    adrp x16, #%d              ; 页基址\n", entry.arm64.adrpImm);
        printf("    ldr  x17, [x16, #%u]        ; 从 GOT[%u] 加载\n",
               entry.arm64.ldrImm, entry.gotIndex);
        printf("    add  x16, x16, #%u          ; x16 = &GOT[%u]\n",
               entry.arm64.addImm, entry.gotIndex);
        printf("    br   x17                    ; 跳转\n");

        if (gotBaseAddr != 0) {
            printf("    ; 计算目标: GOT[0x%lx] = 0x%016lx\n",
                   (unsigned long)entry.gotIndex,
                   (unsigned long)entry.arm64.targetGOT);
        }
    }
}

void PLTTable::printX86_64Assembly(const PLTEntry& entry) const {
    if (entry.index == 0) {
        printf("    pushq GOT[1](%%rip)         ; 压入 link_map\n");
        printf("    jmp   *GOT[2](%%rip)        ; 跳转到 _dl_runtime_resolve\n");
        printf("    nop\n");
    } else {
        printf("    jmp   *0x%x(%%rip)          ; 尝试跳转 GOT[%u]\n",
               entry.x86_64.jmpOffset, entry.gotIndex);
        printf("    pushq $%u                   ; 压入重定位索引\n",
               entry.x86_64.pushIndex);
        printf("    jmp   PLT[0]                ; 转到总服务台\n");
    }
}
