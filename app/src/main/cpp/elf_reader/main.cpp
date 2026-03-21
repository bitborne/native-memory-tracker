// main.cpp - ELF Reader 命令行入口

#include "elf_types.h"
#include "elf_sections.h"
#include "elf_symbols.h"
#include "elf_relocations.h"
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <cstring>

// 打印使用说明
static void printUsage(const char* program) {
    printf("Usage: %s <elf-file> [options]\n", program);
    printf("\nOptions:\n");
    printf("  -h, --header     Display ELF header (default)\n");
    printf("  --help           Display this help\n");
    printf("\nExamples:\n");
    printf("  %s /system/lib64/libc.so\n", program);
    printf("  %s /data/app/.../libdemo_so.so\n", program);
}

// 将整个文件映射到内存
static uint8_t* mmapFile(const char* path, size_t* outSize) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return nullptr;
    }

    struct stat st{};
    if (fstat(fd, &st) < 0) {
        perror("fstat");
        close(fd);
        return nullptr;
    }

    if (st.st_size == 0) {
        fprintf(stderr, "Error: Empty file\n");
        close(fd);
        return nullptr;
    }

    void* mapped = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    if (mapped == MAP_FAILED) {
        perror("mmap");
        return nullptr;
    }

    *outSize = st.st_size;
    return static_cast<uint8_t*>(mapped);
}

// 卸载内存映射
static void unmapFile(uint8_t* data, size_t size) {
    if (data && size > 0) {
        munmap(data, size);
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    // 检查帮助选项
    if (strcmp(argv[1], "--help") == 0) {
        printUsage(argv[0]);
        return 0;
    }

    const char* elfPath = argv[1];

    // 映射文件到内存
    size_t fileSize = 0;
    uint8_t* fileData = mmapFile(elfPath, &fileSize);
    if (!fileData) {
        return 1;
    }

    printf("\nFile: %s\n", elfPath);
    printf("Size: %zu bytes\n\n", fileSize);

    // ==============================
    // 步骤 1: 解析 ELF Header
    // ==============================
    ElfHeader header;
    if (!header.parse(fileData, fileSize)) {
        unmapFile(fileData, fileSize);
        return 1;
    }

    header.print();

    // ==============================
    // 步骤 2: 解析 Section Headers
    // ==============================
    SectionHeaderTable sections;
    if (!sections.parse(fileData, fileSize,
                        header.e_shoff, header.e_shnum, header.e_shentsize,
                        header.e_shstrndx, header.is64bit, header.isLittleEndian)) {
        fprintf(stderr, "Error: Failed to parse section headers\n");
        unmapFile(fileData, fileSize);
        return 1;
    }

    sections.printSections();
    sections.printKeySections();

    // ==============================
    // 步骤 3: 解析动态符号表 (.dynsym)
    // ==============================
    DynamicSymbolTable symtab;
    if (sections.dynsymSection && sections.dynstrSection) {
        const uint8_t* dynsymData = sections.getSectionData(sections.dynsymSection, fileData);
        const uint8_t* dynstrData = sections.getSectionData(sections.dynstrSection, fileData);

        if (dynsymData && dynstrData) {
            if (symtab.parse(dynsymData, sections.dynsymSection->size,
                             dynstrData, sections.dynstrSection->size,
                             header.is64bit, header.isLittleEndian)) {
                symtab.printFunctions();
            }
        }
    }

    // ==============================
    // 步骤 4: 解析重定位表 (.rela.plt)
    // ==============================
    RelocationTable relocs;
    if (sections.relaPltSection) {
        const uint8_t* relaData = sections.getSectionData(sections.relaPltSection, fileData);
        if (relaData) {
            if (relocs.parse(relaData, sections.relaPltSection->size,
                             header.is64bit, header.isLittleEndian, true)) {
                // 关联符号表
                relocs.linkSymbols(symtab);
                relocs.printPLTRelocations();
            }
        }
    }

    // 清理
    unmapFile(fileData, fileSize);

    return 0;
}