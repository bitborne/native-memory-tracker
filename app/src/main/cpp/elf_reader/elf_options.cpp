// elf_options.cpp - 命令行参数解析实现

#include "elf_options.h"
#include <cstdio>
#include <cstring>

// 打印使用说明（简要）
void OptionParser::printUsage(const char* program) {
    printf("Usage: %s [选项] <elf-file>...\n", program);
    printf("   或: %s {-h | -S | -r | -s | -d | -a | -W | -H}\n", program);
    printf("\n");
    printf("显示 ELF 格式文件的信息\n");
    printf("\n");
    printf("  %-20s %s\n", "-a --all", "显示所有信息（等价于 -h -S -s -r -d -D -l）");
    printf("  %-20s %s\n", "-h --file-header", "显示 ELF 文件头");
    printf("  %-20s %s\n", "-S --sections", "显示节头表（section headers）");
    printf("  %-20s %s\n", "-l --segments", "显示程序头表（program headers / segments）");
    printf("  %-20s %s\n", "-s --syms", "显示符号表（symbol table）");
    printf("  %-20s %s\n", "-r --relocs", "显示重定位信息（relocation）");
    printf("  %-20s %s\n", "-d --dynamic", "显示动态段信息（dynamic section）");
    printf("  %-20s %s\n", "-f --eh-frame", "显示 .eh_frame 异常处理帧");
    printf("  %-20s %s\n", "-R --rodata", "显示 .rodata 字符串常量");
    printf("  %-20s %s\n", "-g --debug-line", "显示 DWARF 行号信息 (.debug_line)");
    printf("  %-20s %s\n", "-D --disassemble", "反汇编 PLT（过程链接表）");
    printf("  %-20s %s\n", "-W --wide", "宽格式输出，不截断行");
    printf("  %-20s %s\n", "-H --help", "显示此帮助信息");
    printf("\n");
    printf("示例:\n");
    printf("  %s -h /system/lib64/libc.so          # 显示文件头\n", program);
    printf("  %s -S /system/lib64/libc.so          # 显示节头表\n", program);
    printf("  %s -s /system/lib64/libc.so          # 显示符号表\n", program);
    printf("  %s -r /system/lib64/libc.so          # 显示重定位信息\n", program);
    printf("  %s -a /system/lib64/libc.so          # 显示所有信息\n", program);
    printf("  %s -h -S -s libc.so libdl.so         # 处理多个文件\n", program);
}

// 打印详细帮助
void OptionParser::printHelp(const char* program) {
    printf("ELF Reader - 兼容 GNU readelf 的 ELF 文件分析工具\n");
    printf("\n");
    printUsage(program);
    printf("\n");
    printf("说明:\n");
    printf("  本工具用于解析和分析 ELF 格式文件（可执行文件、共享库、目标文件）\n");
    printf("  支持 32-bit 和 64-bit ELF，支持小端和大端格式\n");
    printf("\n");
    printf("选项详解:\n");
    printf("\n");
    printf("  -a, --all\n");
    printf("      显示所有信息，等价于同时使用 -h -S -s -r -d\n");
    printf("\n");
    printf("  -h, --file-header\n");
    printf("      显示 ELF 文件头（ELF Header）\n");
    printf("      包含：文件类型、目标架构、入口点地址、节头和程序头位置等\n");
    printf("\n");
    printf("  -S, --section-headers, --sections\n");
    printf("      显示节头表（Section Header Table）\n");
    printf("      包含：.text, .data, .dynsym, .plt, .got.plt 等所有节的元数据\n");
    printf("\n");
    printf("  -s, --syms, --symbols\n");
    printf("      显示动态符号表（Dynamic Symbol Table，即 .dynsym）\n");
    printf("      包含：函数名、变量名、绑定类型、所在节区等\n");
    printf("\n");
    printf("  -r, --relocs\n");
    printf("      显示重定位信息（Relocation）\n");
    printf("      包含：PLT 重定位（.rela.plt）和数据重定位（.rela.dyn）\n");
    printf("      显示：重定位类型、符号索引、GOT 偏移等\n");
    printf("\n");
    printf("  -d, --dynamic\n");
    printf("      显示动态段信息（.dynamic section）\n");
    printf("      包含：依赖的共享库、SONAME、符号哈希表位置等\n");
    printf("\n");
    printf("  -W, --wide\n");
    printf("      宽格式输出，不截断行内容\n");
    printf("      默认情况下某些输出可能会被截断以适应终端宽度\n");
    printf("\n");
    printf("  -H, --help\n");
    printf("      显示此帮助信息并退出\n");
    printf("\n");
    printf("默认行为:\n");
    printf("  如果不指定任何显示选项，默认显示 ELF 文件头（等价于 -h）\n");
    printf("\n");
    printf("返回值:\n");
    printf("  0  成功\n");
    printf("  1  命令行错误或文件解析失败\n");
}

// 解析短选项（如 -h, -aSsr）
bool OptionParser::parseShortOption(const char* opt, ReadelfOptions& options) {
    // opt 应该是 "-h" 或 "-aSsr" 这样的格式
    if (opt[0] != '-' || opt[1] == '\0') {
        return false;
    }

    // 处理单个字符选项
    for (size_t i = 1; opt[i] != '\0'; i++) {
        char c = opt[i];
        switch (c) {
            case 'a':
                options.showAll = true;
                break;
            case 'h':
                options.showFileHeader = true;
                break;
            case 'S':
                options.showSectionHeaders = true;
                break;
            case 's':
                options.showSymbols = true;
                break;
            case 'l':
                options.showSegments = true;
                break;
            case 'r':
                options.showRelocs = true;
                break;
            case 'd':
                options.showDynamic = true;
                break;
            case 'f':
                options.showEHFrame = true;
                break;
            case 'R':
                options.showRodata = true;
                break;
            case 'g':
                options.showDebugLine = true;
                break;
            case 'D':
                options.disassemble = true;
                break;
            case 'W':
                options.wideOutput = true;
                break;
            case 'H':
                options.showHelp = true;
                break;
            default:
                fprintf(stderr, "错误: 未知选项 '-%c'\n", c);
                fprintf(stderr, "尝试 '%s --help' 获取更多信息\n", opt);
                return false;
        }
    }
    return true;
}

// 解析长选项（如 --help, --file-header）
bool OptionParser::parseLongOption(const char* opt, ReadelfOptions& options) {
    // opt 应该是 "--help" 这样的格式
    if (strncmp(opt, "--", 2) != 0) {
        return false;
    }

    const char* name = opt + 2;

    if (strcmp(name, "help") == 0) {
        options.showHelp = true;
    } else if (strcmp(name, "all") == 0) {
        options.showAll = true;
    } else if (strcmp(name, "file-header") == 0) {
        options.showFileHeader = true;
    } else if (strcmp(name, "sections") == 0 || strcmp(name, "section-headers") == 0) {
        options.showSectionHeaders = true;
    } else if (strcmp(name, "segments") == 0) {
        options.showSegments = true;
    } else if (strcmp(name, "syms") == 0 || strcmp(name, "symbols") == 0) {
        options.showSymbols = true;
    } else if (strcmp(name, "relocs") == 0) {
        options.showRelocs = true;
    } else if (strcmp(name, "dynamic") == 0) {
        options.showDynamic = true;
    } else if (strcmp(name, "eh-frame") == 0) {
        options.showEHFrame = true;
    } else if (strcmp(name, "rodata") == 0) {
        options.showRodata = true;
    } else if (strcmp(name, "debug-line") == 0) {
        options.showDebugLine = true;
    } else if (strcmp(name, "disassemble") == 0) {
        options.disassemble = true;
    } else if (strcmp(name, "wide") == 0) {
        options.wideOutput = true;
    } else {
        fprintf(stderr, "错误: 未知选项 '%s'\n", opt);
        fprintf(stderr, "尝试 '--help' 获取更多信息\n");
        return false;
    }
    return true;
}

// 解析命令行参数
bool OptionParser::parse(int argc, char* argv[], ReadelfOptions& outOptions) {
    // 重置选项
    outOptions = ReadelfOptions();

    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];

        // 检查是否是选项（以 - 开头）
        if (arg[0] == '-') {
            // 检查是否是长选项（以 -- 开头）
            if (arg[1] == '-') {
                if (!parseLongOption(arg, outOptions)) {
                    return false;
                }
            } else {
                // 短选项
                if (!parseShortOption(arg, outOptions)) {
                    return false;
                }
            }
        } else {
            // 非选项参数，视为文件名
            outOptions.files.push_back(arg);
        }
    }

    // 处理 --all 选项
    if (outOptions.showAll) {
        outOptions.applyAll();
    }

    // 如果没有指定任何显示选项，默认显示文件头
    if (!outOptions.hasDisplayOption() && !outOptions.showHelp) {
        outOptions.showFileHeader = true;
    }

    return true;
}