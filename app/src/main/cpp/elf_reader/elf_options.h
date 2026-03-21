// elf_options.h - 命令行参数解析
// 兼容 GNU readelf 的常用参数风格

#ifndef ELF_OPTIONS_H
#define ELF_OPTIONS_H

#include <cstdint>
#include <vector>
#include <string>

// ========================================
// 命令行选项结构
// ========================================
struct ReadelfOptions {
    // 显示选项
    bool showFileHeader = false;      // -h, --file-header
    bool showSectionHeaders = false;  // -S, --section-headers, --sections
    bool showSymbols = false;         // -s, --syms, --symbols
    bool showRelocs = false;          // -r, --relocs
    bool showDynamic = false;         // -d, --dynamic
    bool showSegments = false;        // -l, --segments (Program Headers)
    bool showEHFrame = false;         // -f, --eh-frame
    bool showRodata = false;          // -R, --rodata
    bool showDebugLine = false;       // -g, --debug-line (DWARF .debug_line)
    bool disassemble = false;         // -D, --disassemble (PLT)
    bool showAll = false;             // -a, --all

    // 输出格式
    bool wideOutput = false;          // -W, --wide (不截断输出)

    // 帮助
    bool showHelp = false;            // -H, --help

    // 文件列表
    std::vector<std::string> files;

    // 检查是否有任何显示选项被设置
    bool hasDisplayOption() const {
        return showFileHeader || showSectionHeaders || showSegments || showSymbols ||
               showRelocs || showDynamic || showEHFrame || showRodata || showDebugLine || disassemble || showAll;
    }

    // 应用 -a (--all) 选项：设置所有显示标志
    void applyAll() {
        showFileHeader = true;
        showSectionHeaders = true;
        showSegments = true;
        showSymbols = true;
        showRelocs = true;
        showDynamic = true;
        showEHFrame = true;
        showRodata = true;
        showDebugLine = true;
        disassemble = true;
    }
};

// ========================================
// 命令行参数解析器
// ========================================
class OptionParser {
public:
    // 解析命令行参数
    // argc, argv: main 函数的参数
    // 返回: true = 解析成功, false = 解析失败或需要显示帮助
    static bool parse(int argc, char* argv[], ReadelfOptions& outOptions);

    // 打印使用说明
    static void printUsage(const char* program);

    // 打印详细帮助
    static void printHelp(const char* program);

private:
    // 解析单个选项
    static bool parseShortOption(const char* opt, ReadelfOptions& options);
    static bool parseLongOption(const char* opt, ReadelfOptions& options);
};

#endif // ELF_OPTIONS_H