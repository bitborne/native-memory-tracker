// elf_segments.cpp - Program Header (Segment) 解析实现

#include "elf_segments.h"
#include <cstdio>

// ========================================
// ProgramHeaderTable 实现
// ========================================

bool ProgramHeaderTable::parse(const uint8_t* data, size_t size,
                               uint64_t phoff, uint16_t phnum, uint16_t phentsize,
                               bool is64bit, bool isLittleEndian) {
    is64bit_ = is64bit;
    isLittleEndian_ = isLittleEndian;

    if (!data || size == 0) {
        fprintf(stderr, "Error: No file data\n");
        return false;
    }

    if (phnum == 0) {
        fprintf(stderr, "Warning: No program headers\n");
        return true;
    }

    // 检查 Program Header 是否在文件范围内
    uint64_t phEnd = phoff + (uint64_t)phnum * phentsize;
    if (phEnd > size) {
        fprintf(stderr, "Error: Program header table extends beyond file\n");
        return false;
    }

    segments.reserve(phnum);

    for (uint16_t i = 0; i < phnum; i++) {
        const uint8_t* entry = data + phoff + i * phentsize;

        SegmentInfo seg;
        seg.index = i;

        if (is64bit) {
            // Elf64_Phdr 布局：
            // offset 0: p_type (4 bytes)
            // offset 4: p_flags (4 bytes)
            // offset 8: p_offset (8 bytes)
            // offset 16: p_vaddr (8 bytes)
            // offset 24: p_paddr (8 bytes)
            // offset 32: p_filesz (8 bytes)
            // offset 40: p_memsz (8 bytes)
            // offset 48: p_align (8 bytes)
            // 总计：56 bytes
            seg.type = readVal<uint32_t>(entry + 0, isLittleEndian);
            seg.flags = readVal<uint32_t>(entry + 4, isLittleEndian);
            seg.offset = readVal<uint64_t>(entry + 8, isLittleEndian);
            seg.vaddr = readVal<uint64_t>(entry + 16, isLittleEndian);
            seg.paddr = readVal<uint64_t>(entry + 24, isLittleEndian);
            seg.filesz = readVal<uint64_t>(entry + 32, isLittleEndian);
            seg.memsz = readVal<uint64_t>(entry + 40, isLittleEndian);
            seg.align = readVal<uint64_t>(entry + 48, isLittleEndian);
        } else {
            // Elf32_Phdr 布局：
            // offset 0: p_type (4 bytes)
            // offset 4: p_offset (4 bytes)
            // offset 8: p_vaddr (4 bytes)
            // offset 12: p_paddr (4 bytes)
            // offset 16: p_filesz (4 bytes)
            // offset 20: p_memsz (4 bytes)
            // offset 24: p_flags (4 bytes)
            // offset 28: p_align (4 bytes)
            // 总计：32 bytes
            seg.type = readVal<uint32_t>(entry + 0, isLittleEndian);
            seg.offset = readVal<uint32_t>(entry + 4, isLittleEndian);
            seg.vaddr = readVal<uint32_t>(entry + 8, isLittleEndian);
            seg.paddr = readVal<uint32_t>(entry + 12, isLittleEndian);
            seg.filesz = readVal<uint32_t>(entry + 16, isLittleEndian);
            seg.memsz = readVal<uint32_t>(entry + 20, isLittleEndian);
            seg.flags = readVal<uint32_t>(entry + 24, isLittleEndian);
            seg.align = readVal<uint32_t>(entry + 28, isLittleEndian);
        }

        // 分类存储
        switch (seg.type) {
            case PT_LOAD:
                loadSegments.push_back(&segments.emplace_back(seg));
                break;
            case PT_DYNAMIC:
                dynamicSegment = &segments.emplace_back(seg);
                break;
            case PT_INTERP:
                interpSegment = &segments.emplace_back(seg);
                break;
            case PT_PHDR:
                phdrSegment = &segments.emplace_back(seg);
                break;
            default:
                segments.emplace_back(seg);
                break;
        }
    }

    return true;
}

const SegmentInfo* ProgramHeaderTable::findByType(uint32_t type) const {
    for (const auto& seg : segments) {
        if (seg.type == type) {
            return &seg;
        }
    }
    return nullptr;
}

const SegmentInfo* ProgramHeaderTable::findLoadSegmentByVAddr(uint64_t vaddr) const {
    for (const auto* seg : loadSegments) {
        if (vaddr >= seg->vaddr && vaddr < seg->vaddr + seg->memsz) {
            return seg;
        }
    }
    return nullptr;
}

const char* ProgramHeaderTable::getTypeName(uint32_t type) {
    switch (type) {
        case PT_NULL:    return "PT_NULL";
        case PT_LOAD:    return "PT_LOAD";
        case PT_DYNAMIC: return "PT_DYNAMIC";
        case PT_INTERP:  return "PT_INTERP";
        case PT_NOTE:    return "PT_NOTE";
        case PT_SHLIB:   return "PT_SHLIB";
        case PT_PHDR:    return "PT_PHDR";
        case PT_TLS:     return "PT_TLS";
        case PT_GNU_EH_FRAME: return "PT_GNU_EH_FRAME";
        case PT_GNU_STACK:    return "PT_GNU_STACK";
        case PT_GNU_RELRO:    return "PT_GNU_RELRO";
        default: {
            static char buf[32];
            if (type >= PT_LOOS && type <= PT_HIOS) {
                snprintf(buf, sizeof(buf), "PT_OS(0x%x)", type);
            } else if (type >= PT_LOPROC && type <= PT_HIPROC) {
                snprintf(buf, sizeof(buf), "PT_PROC(0x%x)", type);
            } else {
                snprintf(buf, sizeof(buf), "PT_UNKNOWN(0x%x)", type);
            }
            return buf;
        }
    }
}

const char* ProgramHeaderTable::getTypeDescription(uint32_t type) {
    switch (type) {
        case PT_NULL:    return "未使用";
        case PT_LOAD:    return "可加载段";
        case PT_DYNAMIC: return "动态链接信息";
        case PT_INTERP:  return "解释器路径";
        case PT_NOTE:    return "辅助信息";
        case PT_SHLIB:   return "共享库(保留)";
        case PT_PHDR:    return "程序头表";
        case PT_TLS:     return "线程本地存储";
        case PT_GNU_EH_FRAME: return "异常处理帧";
        case PT_GNU_STACK:    return "栈标志";
        case PT_GNU_RELRO:    return "只读重定位";
        default: return "";
    }
}

void ProgramHeaderTable::print() const {
    printf("\nProgram Header (Segment) 表:\n");
    printf("  Type           Offset   VirtAddr           PhysAddr           FileSiz  MemSiz   Flg Align  Description\n");
    printf("  -------------- -------- ------------------ ------------------ -------- -------- --- ------ ----------------\n");

    for (const auto& seg : segments) {
        printf("  %-14s 0x%06lx 0x%016lx 0x%016lx 0x%06lx 0x%06lx %s 0x%04lx %s\n",
               getTypeName(seg.type),
               (unsigned long)seg.offset,
               (unsigned long)seg.vaddr,
               (unsigned long)seg.paddr,
               (unsigned long)seg.filesz,
               (unsigned long)seg.memsz,
               seg.getFlagsString().c_str(),
               (unsigned long)seg.align,
               getTypeDescription(seg.type));
    }
}

void ProgramHeaderTable::printLoadSegments() const {
    printf("\nPT_LOAD 段 (运行时内存映射):\n");
    printf("  序号 | 虚拟地址           | 文件偏移 | 文件大小 | 内存大小 | 权限 | 用途\n");
    printf("  -----|--------------------|----------|----------|----------|------|----------\n");

    int idx = 0;
    for (const auto* seg : loadSegments) {
        const char* purpose = "";
        if (seg->isExecutable() && !seg->isWritable()) {
            purpose = "代码段 (.text/.plt)";
        } else if (seg->isWritable() && !seg->isExecutable()) {
            // 数据段：区分 .data 和 .bss
            if (seg->filesz == seg->memsz) {
                purpose = "数据段 (.data)";
            } else if (seg->filesz < seg->memsz) {
                if (seg->filesz == 0) {
                    purpose = "BSS段 (.bss - 零初始化)";
                } else {
                    purpose = "数据段 (.data + .bss)";
                }
            } else {
                purpose = "数据段 (.data/.bss)";
            }
        } else if (!seg->isWritable() && !seg->isExecutable()) {
            purpose = "只读数据 (.rodata/.eh_frame)";
        }

        printf("  [%2d] | 0x%016lx | 0x%06lx | 0x%06lx | 0x%06lx | %s | %s\n",
               idx++,
               (unsigned long)seg->vaddr,
               (unsigned long)seg->offset,
               (unsigned long)seg->filesz,
               (unsigned long)seg->memsz,
               seg->getFlagsString().c_str(),
               purpose);
    }

    printf("\n说明:\n");
    printf("  - 代码段 (R-E): 存放程序指令，可执行但不可写（安全）\n");
    printf("  - 数据段 (RW-): 存放全局变量，可读写但不可执行\n");
    printf("  - BSS 段: memsz > filesz，零初始化数据（节省文件空间）\n");
    printf("  - 只读数据 (R--): 常量字符串等，不可写不可执行\n");
    printf("  - 运行时 linker 将文件偏移映射到虚拟地址\n");
}
