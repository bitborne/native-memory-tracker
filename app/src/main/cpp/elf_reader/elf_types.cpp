// elf_types.cpp - ELF Header 解析实现

#include "elf_types.h"
#include <cstdio>
#include <cstring>

// ========================================
// ElfHeader 实现
// ========================================

bool ElfHeader::parse(const uint8_t* data, size_t size) {
    // 检查文件大小
    if (size < EI_NIDENT) {
        fprintf(stderr, "Error: File too small for ELF magic\n");
        return false;
    }

    // 检查 Magic: 0x7f 'E' 'L' 'F'
    if (memcmp(data, ELFMAG, 4) != 0) {
        fprintf(stderr, "Error: Not an ELF file (bad magic)\n");
        return false;
    }

    // 解析 e_ident[EI_CLASS] - 32/64 位
    ei_class = data[4];
    if (ei_class != ELFCLASS32 && ei_class != ELFCLASS64) {
        fprintf(stderr, "Error: Unknown ELF class: %d\n", ei_class);
        return false;
    }
    is64bit = (ei_class == ELFCLASS64);

    // 解析 e_ident[EI_DATA] - 字节序
    ei_data = data[5];
    if (ei_data != ELFDATA2LSB && ei_data != ELFDATA2MSB) {
        fprintf(stderr, "Error: Unknown data encoding: %d\n", ei_data);
        return false;
    }
    isLittleEndian = (ei_data == ELFDATA2LSB);

    // ELF 版本（应该为 1）
    ei_version = data[6];

    // OS/ABI
    ei_osabi = data[7];

    // 检查最小文件大小
    size_t minSize = is64bit ? sizeof(Elf64_Ehdr) : sizeof(Elf32_Ehdr);
    if (size < minSize) {
        fprintf(stderr, "Error: File too small for ELF header\n");
        return false;
    }

    // 解析 Header 字段
    if (is64bit) {
        // 64-bit ELF Header 解析
        const uint8_t* p = data;
        e_type      = readVal<uint16_t>(p + 16, isLittleEndian);
        e_machine   = readVal<uint16_t>(p + 18, isLittleEndian);
        e_version   = readVal<uint32_t>(p + 20, isLittleEndian);
        e_entry     = readVal<uint64_t>(p + 24, isLittleEndian);
        e_phoff     = readVal<uint64_t>(p + 32, isLittleEndian);
        e_shoff     = readVal<uint64_t>(p + 40, isLittleEndian);
        e_flags     = readVal<uint32_t>(p + 48, isLittleEndian);
        e_ehsize    = readVal<uint16_t>(p + 52, isLittleEndian);
        e_phentsize = readVal<uint16_t>(p + 54, isLittleEndian);
        e_phnum     = readVal<uint16_t>(p + 56, isLittleEndian);
        e_shentsize = readVal<uint16_t>(p + 58, isLittleEndian);
        e_shnum     = readVal<uint16_t>(p + 60, isLittleEndian);
        e_shstrndx  = readVal<uint16_t>(p + 62, isLittleEndian);
    } else {
        // 32-bit ELF Header 解析
        const uint8_t* p = data;
        e_type      = readVal<uint16_t>(p + 16, isLittleEndian);
        e_machine   = readVal<uint16_t>(p + 18, isLittleEndian);
        e_version   = readVal<uint32_t>(p + 20, isLittleEndian);
        e_entry     = readVal<uint32_t>(p + 24, isLittleEndian);
        e_phoff     = readVal<uint32_t>(p + 28, isLittleEndian);
        e_shoff     = readVal<uint32_t>(p + 32, isLittleEndian);
        e_flags     = readVal<uint32_t>(p + 36, isLittleEndian);
        e_ehsize    = readVal<uint16_t>(p + 40, isLittleEndian);
        e_phentsize = readVal<uint16_t>(p + 42, isLittleEndian);
        e_phnum     = readVal<uint16_t>(p + 44, isLittleEndian);
        e_shentsize = readVal<uint16_t>(p + 46, isLittleEndian);
        e_shnum     = readVal<uint16_t>(p + 48, isLittleEndian);
        e_shstrndx  = readVal<uint16_t>(p + 50, isLittleEndian);
    }

    return true;
}

// 获取 ELF 类别名称
const char* ElfHeader::getClassName() const {
    switch (ei_class) {
        case ELFCLASS32: return "ELF32";
        case ELFCLASS64: return "ELF64";
        default: return "Unknown";
    }
}

// 获取字节序名称
const char* ElfHeader::getDataName() const {
    switch (ei_data) {
        case ELFDATA2LSB: return "2's complement, little endian";
        case ELFDATA2MSB: return "2's complement, big endian";
        default: return "Unknown";
    }
}

// 获取文件类型名称
const char* ElfHeader::getTypeName() const {
    switch (e_type) {
        case ET_NONE: return "NONE (None)";
        case ET_REL:  return "REL (Relocatable file)";
        case ET_EXEC: return "EXEC (Executable file)";
        case ET_DYN:  return "DYN (Shared object file)";
        case ET_CORE: return "CORE (Core file)";
        default:      return "Unknown";
    }
}

// 获取架构名称
const char* ElfHeader::getMachineName() const {
    switch (e_machine) {
        case EM_NONE:      return "None";
        case EM_386:       return "Intel 80386";
        case EM_ARM:       return "ARM";
        case EM_X86_64:    return "Advanced Micro Devices X86-64";
        case EM_AARCH64:   return "AArch64";
        default:           return "Unknown";
    }
}

// 获取 OS/ABI 名称
const char* ElfHeader::getOSABIName() const {
    switch (ei_osabi) {
        case 0:  return "UNIX - System V";
        case 3:  return "Linux";
        case 6:  return "Solaris";
        case 9:  return "FreeBSD";
        case 12: return "OpenBSD";
        default: return "Unknown";
    }
}

// 打印 ELF Header 信息（readelf -h 格式）
void ElfHeader::print() const {
    printf("ELF Header:\n");
    printf("  Magic:   ");
    // Magic bytes
    for (int i = 0; i < EI_NIDENT; i++) {
        printf("%02x ", (i == 0) ? 0x7f :
                         (i == 1) ? 'E' :
                         (i == 2) ? 'L' :
                         (i == 3) ? 'F' :
                         (i == 4) ? ei_class :
                         (i == 5) ? ei_data :
                         (i == 6) ? ei_version : 0);
    }
    printf("\n");
    printf("  Class:                             %s\n", getClassName());
    printf("  Data:                              %s\n", getDataName());
    printf("  Version:                           %d (current)\n", ei_version);
    printf("  OS/ABI:                            %s\n", getOSABIName());
    printf("  ABI Version:                       %d\n", 0);
    printf("  Type:                              %s\n", getTypeName());
    printf("  Machine:                           %s\n", getMachineName());
    printf("  Version:                           0x%x\n", e_version);
    printf("  Entry point address:               0x%lx\n", (unsigned long)e_entry);
    printf("  Start of program headers:          %lu (bytes into file)\n", (unsigned long)e_phoff);
    printf("  Start of section headers:          %lu (bytes into file)\n", (unsigned long)e_shoff);
    printf("  Flags:                             0x%x\n", e_flags);
    printf("  Size of this header:               %d (bytes)\n", e_ehsize);
    printf("  Size of program headers:           %d (bytes)\n", e_phentsize);
    printf("  Number of program headers:         %d\n", e_phnum);
    printf("  Size of section headers:           %d (bytes)\n", e_shentsize);
    printf("  Number of section headers:         %d\n", e_shnum);
    printf("  Section header string table index: %d\n", e_shstrndx);
}