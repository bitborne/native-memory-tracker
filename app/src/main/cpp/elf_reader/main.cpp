// main.cpp - ELF Reader 命令行入口
// 兼容 GNU readelf 的参数风格

#include "elf_types.h"
#include "elf_sections.h"
#include "elf_symbols.h"
#include "elf_relocations.h"
#include "elf_plt.h"
#include "elf_dynamic.h"
#include "elf_segments.h"
#include "elf_ehframe.h"
#include "elf_rodata.h"
#include "elf_dwarf.h"
#include "elf_options.h"
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <cstring>

// 将整个文件映射到内存
static uint8_t* mmapFile(const char* path, size_t* outSize) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror(path);
        return nullptr;
    }

    struct stat st{};
    if (fstat(fd, &st) < 0) {
        perror("fstat");
        close(fd);
        return nullptr;
    }

    if (st.st_size == 0) {
        fprintf(stderr, "错误: %s 是空文件\n", path);
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

// 处理单个 ELF 文件
static bool processFile(const char* path, const ReadelfOptions& options) {
    // 映射文件到内存
    size_t fileSize = 0;
    uint8_t* fileData = mmapFile(path, &fileSize);
    if (!fileData) {
        return false;
    }

    printf("\n");
    printf("文件: %s\n", path);
    printf("大小: %zu bytes\n", fileSize);
    printf("\n");

    // ==============================
    // 步骤 1: 解析 ELF Header
    // ==============================
    ElfHeader header;
    if (!header.parse(fileData, fileSize)) {
        unmapFile(fileData, fileSize);
        return false;
    }

    if (options.showFileHeader) {
        header.print();
    }

    // ==============================
    // 步骤 2: 解析 Program Header (Segment)
    // ==============================
    ProgramHeaderTable segmentsPhdr;
    if (options.showSegments) {
        if (header.e_phnum > 0) {
            if (segmentsPhdr.parse(fileData, fileSize,
                                   header.e_phoff, header.e_phnum, header.e_phentsize,
                                   header.is64bit, header.isLittleEndian)) {
                segmentsPhdr.print();
                segmentsPhdr.printLoadSegments();
            }
        } else {
            printf("此文件没有 Program Header\n\n");
        }
    }

    // 如果只需要显示文件头或段，可以跳过节解析
    bool needSections = options.showSectionHeaders || options.showSymbols ||
                        options.showRelocs || options.showDynamic || options.disassemble ||
                        options.showEHFrame || options.showRodata || options.showDebugLine;

    if (!needSections) {
        unmapFile(fileData, fileSize);
        return true;
    }

    // ==============================
    // 步骤 2: 解析 Section Headers
    // ==============================
    SectionHeaderTable sections;
    if (!sections.parse(fileData, fileSize,
                        header.e_shoff, header.e_shnum, header.e_shentsize,
                        header.e_shstrndx, header.is64bit, header.isLittleEndian)) {
        fprintf(stderr, "错误: 解析节头表失败\n");
        unmapFile(fileData, fileSize);
        return false;
    }

    if (options.showSectionHeaders) {
        sections.printSections();
        sections.printKeySections();
    }

    // ==============================
    // 步骤 3: 解析动态符号表 (.dynsym)
    // ==============================
    DynamicSymbolTable symtab;
    if (sections.dynsymSection && sections.dynstrSection) {
        const uint8_t* dynsymData = sections.getSectionData(sections.dynsymSection, fileData);
        const uint8_t* dynstrData = sections.getSectionData(sections.dynstrSection, fileData);

        if (dynsymData && dynstrData) {
            symtab.parse(dynsymData, sections.dynsymSection->size,
                        dynstrData, sections.dynstrSection->size,
                        header.is64bit, header.isLittleEndian);
        }
    }

    if (options.showSymbols) {
        if (symtab.symbols.empty()) {
            printf("此文件没有动态符号表 (.dynsym)\n\n");
        } else {
            symtab.printSymbols();
        }
    }

    // ==============================
    // 步骤 4: 解析重定位表 (.rela.plt 和 .rela.dyn)
    // ==============================
    RelocationTable relaPlt;  // PLT 重定位（函数）
    RelocationTable relaDyn;  // 数据重定位（全局变量）

    if (sections.relaPltSection) {
        const uint8_t* relaData = sections.getSectionData(sections.relaPltSection, fileData);
        if (relaData) {
            relaPlt.parse(relaData, sections.relaPltSection->size,
                        header.is64bit, header.isLittleEndian, true, header.e_machine);
            relaPlt.linkSymbols(symtab);
        }
    }

    if (sections.relaDynSection) {
        const uint8_t* relaData = sections.getSectionData(sections.relaDynSection, fileData);
        if (relaData) {
            relaDyn.parse(relaData, sections.relaDynSection->size,
                        header.is64bit, header.isLittleEndian, false, header.e_machine);
            relaDyn.linkSymbols(symtab);
        }
    }

    if (options.showRelocs) {
        if (!relaDyn.relocations.empty()) {
            relaDyn.printRelocations();
        }
        if (!relaPlt.relocations.empty()) {
            relaPlt.printRelocations();
        }
        if (relaPlt.relocations.empty() && relaDyn.relocations.empty()) {
            printf("此文件没有重定位信息\n\n");
        }
    }

    // ==============================
    // 步骤 5: 解析 .dynamic 段
    // ==============================
    DynamicTable dynamic;
    if (sections.dynamicSection && sections.dynstrSection) {
        const uint8_t* dynamicData = sections.getSectionData(sections.dynamicSection, fileData);
        const uint8_t* dynstrData = sections.getSectionData(sections.dynstrSection, fileData);

        if (dynamicData && dynstrData) {
            dynamic.parse(dynamicData, sections.dynamicSection->size,
                         dynstrData, sections.dynstrSection->size,
                         header.is64bit, header.isLittleEndian);
        }
    }

    if (options.showDynamic) {
        if (!dynamic.entries.empty()) {
            dynamic.printNeededLibs();
            printf("\n");
            dynamic.print();
        } else {
            printf("此文件没有动态段 (.dynamic)\n\n");
        }
    }

    // ==============================
    // 步骤 6: PLT 反汇编
    // ==============================
    if (options.disassemble) {
        if (sections.pltSection && sections.gotPltSection) {
            PLTTable plt;
            plt.setGOTBase(sections.gotPltSection->addr);

            const uint8_t* pltData = sections.getSectionData(sections.pltSection, fileData);
            if (pltData) {
                if (plt.parse(pltData, sections.pltSection->size,
                             sections.pltSection->addr, header.is64bit,
                             header.isLittleEndian, header.e_machine)) {
                    plt.print();

                    // 如果同时有重定位表，进行一致性验证
                    if (!relaPlt.relocations.empty()) {
                        plt.verifyAgainstRelocations(relaPlt);
                    }
                }
            }
        } else {
            printf("此文件没有 PLT 或 GOT.plt 节\n\n");
        }
    }

    // ==============================
    // 步骤 7: .eh_frame 解析
    // ==============================
    if (options.showEHFrame) {
        // 先尝试找 .eh_frame，如果没有则尝试 .gnu.eh_frame_hdr
        const SectionInfo* ehFrameSection = sections.findByName(".eh_frame");
        if (ehFrameSection) {
            const uint8_t* ehFrameData = sections.getSectionData(ehFrameSection, fileData);
            if (ehFrameData) {
                EHFrameParser ehFrame;
                if (ehFrame.parse(ehFrameData, ehFrameSection->size,
                                  ehFrameSection->addr, header.is64bit, header.isLittleEndian)) {
                    ehFrame.print();
                }
            }
        } else {
            printf("此文件没有 .eh_frame 节\n\n");
        }
    }

    // ==============================
    // 步骤 8: DWARF .debug_line 解析
    // ==============================
    if (options.showDebugLine) {
        const SectionInfo* debugLineSection = sections.findByName(".debug_line");

        if (debugLineSection) {
            const uint8_t* dbgData = sections.getSectionData(debugLineSection, fileData);
            if (dbgData) {
                DwarfLineParser dwarf;
                if (dwarf.parse(dbgData, debugLineSection->size,
                                header.is64bit, header.isLittleEndian)) {
                    dwarf.printSummary();
                }
            }
        } else {
            printf("此文件没有 .debug_line 节（发布版 so 通常已裁剪调试信息）\n\n");
        }
    }

    // ==============================
    // 步骤 9: .rodata 解析
    // ==============================
    if (options.showRodata) {
        const SectionInfo* rodataSection = sections.findByName(".rodata");

        if (rodataSection) {
            const uint8_t* rodataData = sections.getSectionData(rodataSection, fileData);
            if (rodataData) {
                RodataParser rodata;
                if (rodata.parse(rodataData, rodataSection->size,
                                rodataSection->addr, header.isLittleEndian)) {
                    rodata.extractStrings(4);  // 提取长度 >= 4 的字符串
                    rodata.printStrings(50);   // 打印前 50 个
                }
            }
        } else {
            printf("此文件没有 .rodata 节\n\n");
        }
    }

    // 清理
    unmapFile(fileData, fileSize);

    return true;
}

int main(int argc, char* argv[]) {
    // 解析命令行参数
    ReadelfOptions options;
    if (!OptionParser::parse(argc, argv, options)) {
        OptionParser::printUsage(argv[0]);
        return 1;
    }

    // 显示帮助
    if (options.showHelp) {
        OptionParser::printHelp(argv[0]);
        return 0;
    }

    // 检查是否有文件
    if (options.files.empty()) {
        fprintf(stderr, "错误: 未指定 ELF 文件\n");
        fprintf(stderr, "\n");
        OptionParser::printUsage(argv[0]);
        return 1;
    }

    // 处理每个文件
    bool allSuccess = true;
    for (const auto& file : options.files) {
        if (!processFile(file.c_str(), options)) {
            allSuccess = false;
        }
    }

    return allSuccess ? 0 : 1;
}