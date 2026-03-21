// elf_ehframe.h - .eh_frame 解析
// 理解 C++ 异常处理和栈回溯机制

#ifndef ELF_EHFRAME_H
#define ELF_EHFRAME_H

#include "elf_types.h"
#include <vector>
#include <string>
#include <memory>

// ========================================
// CIE/FDE 通用头部
// ========================================
struct EHFrameEntry {
    uint64_t offset;           // 在文件中的偏移
    uint64_t length;           // 条目长度（不含自身）
    uint32_t cieId;            // CIE ID（CIE=0，FDE=指向CIE的偏移）
    bool isCIE;                // 是否是 CIE

    EHFrameEntry() : offset(0), length(0), cieId(0), isCIE(false) {}
};

// ========================================
// CIE (Common Information Entry)
// ========================================
struct CIEEntry : EHFrameEntry {
    uint8_t version;                    // 版本号（通常是 1 或 3）
    std::string augmentation;           // 扩展字符串（如 "zPLR"）
    uint64_t codeAlignmentFactor;       // 代码对齐因子
    int64_t  dataAlignmentFactor;       // 数据对齐因子（有符号）
    uint64_t returnAddressRegister;     // 返回地址寄存器
    std::vector<uint8_t> initialInstructions; // 初始指令

    // 扩展字段（当 augmentation 以 'z' 开头）
    uint64_t augmentationLength;        // 扩展数据长度
    uint64_t personalityFunction;       // personality 函数地址
    uint8_t  personalityEncoding;       // personality 地址编码
    uint8_t  lsdaEncoding;              // LSDA 编码方式
    uint8_t  fdeEncoding;               // FDE 编码方式
    bool hasAugmentationData;           // 是否有扩展数据
    bool hasPersonality;                // 是否有 personality

    CIEEntry() : version(0), codeAlignmentFactor(0), dataAlignmentFactor(0),
                 returnAddressRegister(0), augmentationLength(0),
                 personalityFunction(0), personalityEncoding(0),
                 lsdaEncoding(0), fdeEncoding(0), hasAugmentationData(false),
                 hasPersonality(false) {
        isCIE = true;
    }
};

// ========================================
// FDE (Frame Description Entry)
// ========================================
struct FDEEntry : EHFrameEntry {
    const CIEEntry* cie;                // 关联的 CIE
    uint64_t pcBegin;                   // 函数起始地址（PC）
    uint64_t pcRange;                   // 函数大小
    uint64_t lsdaPointer;               // LSDA 指针（C++ 异常表）
    std::vector<uint8_t> instructions;  // 栈展开指令

    FDEEntry() : cie(nullptr), pcBegin(0), pcRange(0), lsdaPointer(0) {
        isCIE = false;
    }
};

// ========================================
// .eh_frame 解析器
// ========================================
class EHFrameParser {
public:
    std::vector<std::unique_ptr<CIEEntry>> cies;
    std::vector<std::unique_ptr<FDEEntry>> fdes;

private:
    bool is64bit_ = false;
    bool isLittleEndian_ = true;
    uint64_t sectionAddr_ = 0;

public:
    // 解析 .eh_frame section
    bool parse(const uint8_t* data, size_t size,
               uint64_t sectionAddr, bool is64bit, bool isLittleEndian);

    // 查找包含给定 PC 的 FDE
    const FDEEntry* findFDEByPC(uint64_t pc) const;

    // 获取 FDE 数量
    size_t getFDECount() const { return fdes.size(); }

    // 获取 CIE 数量
    size_t getCIECount() const { return cies.size(); }

    // 打印所有条目（简略）
    void print() const;

    // 打印摘要信息
    void printSummary() const;

    // 打印指定 FDE 的详细信息
    void printFDE(const FDEEntry* fde) const;

private:
    // 解析 CIE
    bool parseCIE(CIEEntry* cie, const uint8_t* data, size_t size, size_t& pos);

    // 解析 FDE
    bool parseFDE(FDEEntry* fde, const uint8_t* data, size_t size, size_t& pos,
                  const CIEEntry* cie);

    // 解析 augmentation 字符串
    bool parseAugmentation(CIEEntry* cie, const uint8_t* data, size_t size, size_t& pos);

    // 读取 ULEB128 编码
    static uint64_t readULEB128(const uint8_t* data, size_t size, size_t& pos);

    // 读取 SLEB128 编码
    static int64_t readSLEB128(const uint8_t* data, size_t size, size_t& pos);

    // 读取编码后的地址
    uint64_t readEncodedAddress(const uint8_t* data, size_t size, size_t& pos,
                                uint8_t encoding, uint64_t base) const;

    // 获取编码方式的大小
    static int getEncodingSize(uint8_t encoding);

    // 获取编码方式的名称
    static const char* getEncodingName(uint8_t encoding);
};

#endif // ELF_EHFRAME_H
