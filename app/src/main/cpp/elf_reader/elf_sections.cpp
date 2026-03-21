// elf_sections.cpp - Section Header 解析实现

#include "elf_sections.h"
#include <cstdio>
#include <cstring>

// ========================================
// SectionHeaderTable 实现
// ========================================

bool SectionHeaderTable::parse(const uint8_t* data, size_t size,
                               uint64_t shoff, uint16_t shnum, uint16_t shentsize,
                               uint16_t shstrndx, bool is64bit, bool isLittleEndian) {

    /* 目前已知:
        e_shoff: 节头表在文件中的偏移
        e_shnum: 节条目数
        e_shentsize: 每一个节头的大小 `Size of section headers: 64 (bytes)`
        e_shstrndx: section 名称字符串表在节头表中的偏移
    */

    is64bit_ = is64bit;
    isLittleEndian_ = isLittleEndian;

    if (shnum == 0) {
        printf("No section headers in this file\n");
        return true;
    }

    // 检查section header table范围
    size_t tableSize = (size_t)shentsize * shnum;
    if (shoff + tableSize > size) {
        fprintf(stderr, "Error: Section header table extends beyond file\n");
        return false;
    }

    // 避免填充时扩容
    sections.reserve(shnum);

    // 第一遍：解析所有section header（此时还不知道名称）
    for (uint32_t i = 0; i < shnum; i++) {
        const uint8_t* shdr = data + shoff + (size_t)shentsize * i;

        SectionInfo sec;
        sec.index = i;
         // 每个header总共十个字段, 本次循环全部填入
        if (is64bit) {
            sec.type = readVal<uint32_t>(shdr + 4, isLittleEndian);
            sec.flags = readVal<uint64_t>(shdr + 8, isLittleEndian);
            sec.addr = readVal<uint64_t>(shdr + 16, isLittleEndian);
            sec.offset = readVal<uint64_t>(shdr + 24, isLittleEndian);
            sec.size = readVal<uint64_t>(shdr + 32, isLittleEndian);
            sec.link = readVal<uint32_t>(shdr + 40, isLittleEndian);
            sec.info = readVal<uint32_t>(shdr + 44, isLittleEndian);
            sec.addralign = readVal<uint64_t>(shdr + 48, isLittleEndian);
            sec.entsize = readVal<uint64_t>(shdr + 56, isLittleEndian);
            // 名称偏移最后保存
            uint32_t nameOffset = readVal<uint32_t>(shdr + 0, isLittleEndian);
            // 各个sec的name,先存的是nameOffset: 在section名称字符串表中的偏移
            // 后面会解析成真名(.dynsym .rela.plt .dynstr 等)
            sec.name = std::to_string(nameOffset);  // 临时存储偏移值
        } else {
            uint32_t nameOffset = readVal<uint32_t>(shdr + 0, isLittleEndian);
            sec.type = readVal<uint32_t>(shdr + 4, isLittleEndian);
            sec.flags = readVal<uint32_t>(shdr + 8, isLittleEndian);
            sec.addr = readVal<uint32_t>(shdr + 12, isLittleEndian);
            sec.offset = readVal<uint32_t>(shdr + 16, isLittleEndian);
            sec.size = readVal<uint32_t>(shdr + 20, isLittleEndian);
            sec.link = readVal<uint32_t>(shdr + 24, isLittleEndian);
            sec.info = readVal<uint32_t>(shdr + 28, isLittleEndian);
            sec.addralign = readVal<uint32_t>(shdr + 32, isLittleEndian);
            sec.entsize = readVal<uint32_t>(shdr + 36, isLittleEndian);
            sec.name = std::to_string(nameOffset);
        }

        // 将填好的单子上交保存
        sections.push_back(sec);
    }

    // 找到 section 名称字符串表的位置(shstrndx 是 ELF header 传进来的)
    if (shstrndx != 0 && shstrndx < shnum) {
        shstrtabSection = &sections[shstrndx];
        // 加载字符串表数据
        if (!loadShstrtab(data, size)) {
            fprintf(stderr, "Warning: Failed to load section name string table\n");
        }
    }

    // 第二遍(非必须)：解析section名称并识别关键section
    for (auto& sec : sections) {
        // 解析名称
        if (!shstrtabData_.empty()) {
            uint32_t nameOffset = std::stoul(sec.name);
            const char* name = getString(shstrtabData_.data(), shstrtabData_.size(), nameOffset);
            sec.name = name ? name : "";
        }

        // 识别关键section
        if (sec.name == ".shstrtab") {
            // section名称字符串表已在上面处理(位置:第一遍解析结束后,加载section名称字符串表内容前)
        } else if (sec.name == ".dynstr") {
            dynstrSection = &sec;
        } else if (sec.name == ".dynsym") {
            dynsymSection = &sec;
        } else if (sec.name == ".rela.plt") {
            relaPltSection = &sec;
        } else if (sec.name == ".rela.dyn") {
            relaDynSection = &sec;
        } else if (sec.name == ".plt") {
            pltSection = &sec;
        } else if (sec.name == ".got.plt") {
            gotPltSection = &sec;
        } else if (sec.name == ".dynamic") {
            dynamicSection = &sec;
        } else if (sec.name == ".hash") {
            hashSection = &sec;
        } else if (sec.name == ".gnu.hash") {
            gnuHashSection = &sec;
        }
    }

    return true;
}

bool SectionHeaderTable::loadShstrtab(const uint8_t* data, size_t size) {
    // data: mmap 起点地址
    // size: 文件大小

    // 参数合法性验证
    if (!shstrtabSection || shstrtabSection->offset == 0 || shstrtabSection->size == 0) {
        return false;
    }

    // 还是合不合法的验证, 不能超出文件大小了
    if (shstrtabSection->offset + shstrtabSection->size > size) {
        fprintf(stderr, "Error: Section name string table extends beyond file\n");
        return false;
    }

    // 这里的 ->size 指的是整个 字符串表这个section 的大小
    // shstrtabData_ 是一个字节数组, 存储整个 section 的数据, 而非单个header条目的数据
    shstrtabData_.resize(shstrtabSection->size);
    memcpy(shstrtabData_.data(), data + shstrtabSection->offset, shstrtabSection->size);
    return true;
}

const char* SectionHeaderTable::getString(const uint8_t* strtab, size_t strtabSize, uint32_t offset) {
    if (offset >= strtabSize) {
        return nullptr;
    }
    // 确保以null结尾
    const char* str = reinterpret_cast<const char*>(strtab + offset);
    // 检查字符串是否在范围内
    size_t maxLen = strtabSize - offset;
    if (strnlen(str, maxLen) >= maxLen) {
        return nullptr;  // 未在范围内找到null终止符
    }
    return str;
}

const SectionInfo* SectionHeaderTable::findByName(const char* name) const {
    for (const auto& sec : sections) {
        if (sec.name == name) {
            return &sec;
        }
    }
    return nullptr;
}

const uint8_t* SectionHeaderTable::getSectionData(const SectionInfo* sec, const uint8_t* fileData) const {
    if (!sec || sec->offset == 0 || sec->size == 0) {
        return nullptr;
    }
    return fileData + sec->offset;
}

const char* SectionHeaderTable::getSectionTypeName(uint32_t type) const {
    switch (type) {
        case SHT_NULL: return "NULL";
        case SHT_PROGBITS: return "PROGBITS";
        case SHT_SYMTAB: return "SYMTAB";
        case SHT_STRTAB: return "STRTAB";
        case SHT_RELA: return "RELA";
        case SHT_HASH: return "HASH";
        case SHT_DYNAMIC: return "DYNAMIC";
        case SHT_NOTE: return "NOTE";
        case SHT_NOBITS: return "NOBITS";
        case SHT_REL: return "REL";
        case SHT_SHLIB: return "SHLIB";
        case SHT_DYNSYM: return "DYNSYM";
        case SHT_INIT_ARRAY: return "INIT_ARRAY";
        case SHT_FINI_ARRAY: return "FINI_ARRAY";
        case SHT_PREINIT_ARRAY: return "PREINIT_ARRAY";
        case SHT_GROUP: return "GROUP";
        case SHT_SYMTAB_SHNDX: return "SYMTAB_SHNDX";
        case SHT_GNU_HASH: return "GNU_HASH";
        default:
            if (type >= 0x60000000 && type < 0x70000000) {
                return "LOOS+";
            } else if (type >= 0x70000000 && type < 0x80000000) {
                return "LOPROC+";
            }
            return "UNKNOWN";
    }
}

std::string SectionHeaderTable::getSectionFlags(uint64_t flags) const {
    std::string result;
    if (flags & SHF_WRITE) result += 'W';
    if (flags & SHF_ALLOC) result += 'A';
    if (flags & SHF_EXECINSTR) result += 'X';
    if (flags & SHF_MERGE) result += 'M';
    if (flags & SHF_STRINGS) result += 'S';
    if (flags & SHF_INFO_LINK) result += 'I';
    if (flags & SHF_LINK_ORDER) result += 'L';
    if (flags & SHF_OS_NONCONFORMING) result += 'O';
    if (flags & SHF_GROUP) result += 'G';
    if (flags & SHF_TLS) result += 'T';
    if (flags & SHF_EXCLUDE) result += 'E';
    return result;
}

void SectionHeaderTable::printSections() const {
    printf("\nSection Headers:\n");
    printf("  [Nr] Name              Type            Address          Off    Size   ES Flg Lk Inf Al\n");

    for (const auto& sec : sections) {
        printf("  [%2u] %-17s %-15s %016lx %06lx %06lx %02lx %3s %2u %3u %2lu\n",
               sec.index,
               sec.name.c_str(),
               getSectionTypeName(sec.type),
               (unsigned long)sec.addr,
               (unsigned long)sec.offset,
               (unsigned long)sec.size,
               (unsigned long)sec.entsize,
               getSectionFlags(sec.flags).c_str(),
               sec.link,
               sec.info,
               (unsigned long)sec.addralign);
    }

    printf("Key to Flags:\n");
    printf("  W (write), A (alloc), X (execute), M (merge), S (strings), I (info),\n");
    printf("  L (link order), O (extra OS processing required), G (group), T (TLS),\n");
    printf("  E (exclude), C (compressed)\n");
}

void SectionHeaderTable::printKeySections() const {
    printf("\nKey Sections for Dynamic Linking:\n");
    printf("  %-15s %s\n", ".dynsym", dynsymSection ? "present" : "NOT FOUND");
    printf("  %-15s %s\n", ".dynstr", dynstrSection ? "present" : "NOT FOUND");
    printf("  %-15s %s\n", ".rela.plt", relaPltSection ? "present" : "NOT FOUND");
    printf("  %-15s %s\n", ".rela.dyn", relaDynSection ? "present" : "NOT FOUND");
    printf("  %-15s %s\n", ".plt", pltSection ? "present" : "NOT FOUND");
    printf("  %-15s %s\n", ".got.plt", gotPltSection ? "present" : "NOT FOUND");
    printf("  %-15s %s\n", ".dynamic", dynamicSection ? "present" : "NOT FOUND");
    printf("  %-15s %s\n", ".hash", hashSection ? "present" : "NOT FOUND");
    printf("  %-15s %s\n", ".gnu.hash", gnuHashSection ? "present" : "NOT FOUND");
}
