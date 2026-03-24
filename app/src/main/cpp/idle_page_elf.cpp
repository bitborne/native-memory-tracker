//
// idle_page_elf.cpp
// ELF 节区解析实现
//

#include "idle_page_elf.h"
#include "idle_page_log.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <cstring>
#include <cstdio>

namespace idle_page {

// ==================== ElfSectionMapper ====================

ElfSectionMapper::ElfSectionMapper() = default;

ElfSectionMapper::~ElfSectionMapper() {
    unload();
}

bool ElfSectionMapper::load(const char* so_path) {
    unload();

    fd_ = ::open(so_path, O_RDONLY);
    if (fd_ < 0) {
        // 尝试在 /data/app 下查找
        char app_path[256];
        snprintf(app_path, sizeof(app_path), "/data/app/%s", so_path);
        fd_ = ::open(app_path, O_RDONLY);

        if (fd_ < 0) {
            IDLE_LOGE("Failed to open %s", so_path);
            return false;
        }
    }

    struct stat st;
    if (fstat(fd_, &st) < 0) {
        IDLE_LOGE("fstat failed for %s", so_path);
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    mmap_size_ = st.st_size;
    mmap_base_ = mmap(nullptr, mmap_size_, PROT_READ, MAP_PRIVATE, fd_, 0);

    if (mmap_base_ == MAP_FAILED) {
        IDLE_LOGE("mmap failed for %s", so_path);
        ::close(fd_);
        fd_ = -1;
        mmap_base_ = nullptr;
        return false;
    }

    if (!parse_elf()) {
        unload();
        return false;
    }

    IDLE_LOGI("Loaded ELF sections from %s", so_path);
    return true;
}

void ElfSectionMapper::unload() {
    if (mmap_base_) {
        munmap(mmap_base_, mmap_size_);
        mmap_base_ = nullptr;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    sections_.clear();
    load_base_ = 0;
}

std::string ElfSectionMapper::get_section_perms(uint32_t sh_flags) {
    std::string perms = "----";

    // 代码段通常有执行权限
    if (sh_flags & SHF_EXECINSTR) {
        perms[2] = 'x';
    }

    // 可分配段
    if (sh_flags & SHF_ALLOC) {
        perms[3] = 'p';  // private
    }

    // 写权限
    if (sh_flags & SHF_WRITE) {
        perms[1] = 'w';
    }

    // 读权限（默认有）
    perms[0] = 'r';

    return perms;
}

bool ElfSectionMapper::parse_elf() {
    if (!mmap_base_ || mmap_size_ < sizeof(Elf64_Ehdr)) {
        return false;
    }

    const uint8_t* data = static_cast<const uint8_t*>(mmap_base_);

    // 检查 ELF 魔数
    if (data[0] != 0x7f || data[1] != 'E' || data[2] != 'L' || data[3] != 'F') {
        IDLE_LOGE("Not a valid ELF file");
        return false;
    }

    bool is64bit = (data[4] == ELFCLASS64);
    bool isLittleEndian = (data[5] == ELFDATA2LSB);

    if (is64bit) {
        const Elf64_Ehdr* ehdr = reinterpret_cast<const Elf64_Ehdr*>(data);

        // 读取节区表信息
        uint64_t shoff = ehdr->e_shoff;
        uint16_t shnum = ehdr->e_shnum;
        uint16_t shentsize = ehdr->e_shentsize;
        uint16_t shstrndx = ehdr->e_shstrndx;

        if (shoff == 0 || shnum == 0) {
            IDLE_LOGE("No section headers");
            return false;
        }

        // 读取节区表
        const Elf64_Shdr* shdrs = reinterpret_cast<const Elf64_Shdr*>(data + shoff);

        // 读取字符串表
        const char* shstrtab = nullptr;
        if (shstrndx < shnum) {
            const Elf64_Shdr& strtab_hdr = shdrs[shstrndx];
            shstrtab = reinterpret_cast<const char*>(data + strtab_hdr.sh_offset);
        }

        // 解析每个 ALLOC 节区
        for (uint16_t i = 0; i < shnum; ++i) {
            const Elf64_Shdr& shdr = shdrs[i];

            // 只关注需要加载到内存的节区
            if ((shdr.sh_flags & SHF_ALLOC) && shdr.sh_size > 0) {
                SectionMapping mapping;
                mapping.start = shdr.sh_addr;  // 文件中的虚拟地址（需要加上加载基址）
                mapping.end = shdr.sh_addr + shdr.sh_size;
                mapping.perms = get_section_perms(shdr.sh_flags);

                if (shstrtab && shdr.sh_name > 0) {
                    mapping.name = shstrtab + shdr.sh_name;
                } else {
                    mapping.name = "UNKNOWN";
                }

                sections_.push_back(mapping);
            }
        }

    } else {
        // 32-bit ELF
        const Elf32_Ehdr* ehdr = reinterpret_cast<const Elf32_Ehdr*>(data);

        uint32_t shoff = ehdr->e_shoff;
        uint16_t shnum = ehdr->e_shnum;
        uint16_t shstrndx = ehdr->e_shstrndx;

        if (shoff == 0 || shnum == 0) {
            return false;
        }

        const Elf32_Shdr* shdrs = reinterpret_cast<const Elf32_Shdr*>(data + shoff);
        const char* shstrtab = nullptr;

        if (shstrndx < shnum) {
            const Elf32_Shdr& strtab_hdr = shdrs[shstrndx];
            shstrtab = reinterpret_cast<const char*>(data + strtab_hdr.sh_offset);
        }

        for (uint16_t i = 0; i < shnum; ++i) {
            const Elf32_Shdr& shdr = shdrs[i];

            if ((shdr.sh_flags & SHF_ALLOC) && shdr.sh_size > 0) {
                SectionMapping mapping;
                mapping.start = shdr.sh_addr;
                mapping.end = shdr.sh_addr + shdr.sh_size;
                mapping.perms = get_section_perms(shdr.sh_flags);

                if (shstrtab && shdr.sh_name > 0) {
                    mapping.name = shstrtab + shdr.sh_name;
                } else {
                    mapping.name = "UNKNOWN";
                }

                sections_.push_back(mapping);
            }
        }
    }

    IDLE_LOGI("Parsed %zu ELF sections", sections_.size());
    return !sections_.empty();
}

const char* ElfSectionMapper::lookup_section(uintptr_t vaddr) const {
    // vaddr 是相对地址，需要减去 load_base_
    uintptr_t rel_addr = vaddr - load_base_;

    for (const auto& sec : sections_) {
        if (rel_addr >= sec.start && rel_addr < sec.end) {
            return sec.name.c_str();
        }
    }

    return "UNKNOWN";
}

const char* ElfSectionMapper::lookup_perms(uintptr_t vaddr) const {
    uintptr_t rel_addr = vaddr - load_base_;

    for (const auto& sec : sections_) {
        if (rel_addr >= sec.start && rel_addr < sec.end) {
            return sec.perms.c_str();
        }
    }

    return "----";
}

void ElfSectionMapper::dump_sections() const {
    IDLE_LOGI("ELF Sections (load_base=0x%llx):", (unsigned long long)load_base_);
    for (const auto& sec : sections_) {
        IDLE_LOGI("  0x%08llx-0x%08llx %s %s",
                  (unsigned long long)sec.start,
                  (unsigned long long)sec.end,
                  sec.perms.c_str(),
                  sec.name.c_str());
    }
}

// ==================== RuntimeSectionResolver ====================

bool RuntimeSectionResolver::init(const char* so_name) {
    // 1. 获取运行时内存区域
    if (!ProcMapsParser::find_so_regions(so_name, runtime_regions_)) {
        return false;
    }

    // 2. 尝试加载 SO 文件解析节区
    // 先尝试从 /proc/self/maps 中的路径
    std::string so_path;
    for (const auto& region : runtime_regions_) {
        if (!region.name.empty() && region.name.find(so_name) != std::string::npos) {
            so_path = region.name;
            break;
        }
    }

    if (!so_path.empty()) {
        elf_mapper_.load(so_path.c_str());
    }

    // 3. 计算加载基址
    // 通常是第一个区域的 start 地址减去 ELF 的第一个 PT_LOAD 段的虚拟地址
    // 简化处理：使用第一个可执行区域的 start 作为基址
    if (!runtime_regions_.empty()) {
        uintptr_t runtime_base = runtime_regions_[0].start;

        // 如果 ELF 解析成功，计算偏移
        if (elf_mapper_.is_loaded() && !elf_mapper_.get_sections().empty()) {
            // 找到第一个可执行节区（通常是 .text）
            for (const auto& sec : elf_mapper_.get_sections()) {
                if (sec.perms.find('x') != std::string::npos) {
                    // 计算加载基址 = 运行时地址 - 文件地址
                    uintptr_t file_addr = sec.start;
                    elf_mapper_.load_base_ = runtime_base - file_addr;
                    break;
                }
            }
        }
    }

    return true;
}

std::string RuntimeSectionResolver::resolve(uintptr_t vaddr) const {
    const char* name = get_name(vaddr);
    const char* perms = get_perms(vaddr);

    char result[128];
    snprintf(result, sizeof(result), "%s (%s)", name, perms);
    return std::string(result);
}

const char* RuntimeSectionResolver::get_name(uintptr_t vaddr) const {
    // 优先使用 ELF 解析的节区名称
    if (elf_mapper_.is_loaded()) {
        return elf_mapper_.lookup_section(vaddr);
    }

    // 回退到运行时区域名
    for (const auto& region : runtime_regions_) {
        if (vaddr >= region.start && vaddr < region.end) {
            // 从 path 中提取文件名
            size_t pos = region.name.find_last_of('/');
            if (pos != std::string::npos) {
                return region.name.c_str() + pos + 1;
            }
            return region.name.c_str();
        }
    }

    return "UNKNOWN";
}

const char* RuntimeSectionResolver::get_perms(uintptr_t vaddr) const {
    // 优先使用 ELF 解析的权限
    if (elf_mapper_.is_loaded()) {
        const char* perms = elf_mapper_.lookup_perms(vaddr);
        if (strcmp(perms, "----") != 0) {
            return perms;
        }
    }

    // 回退到运行时权限
    for (const auto& region : runtime_regions_) {
        if (vaddr >= region.start && vaddr < region.end) {
            return region.perms;
        }
    }

    return "----";
}

} // namespace idle_page