# ELF Reader - 支持编译成独立 binary 或 library

cmake_minimum_required(VERSION 3.22.1)

# 源文件列表
set(ELF_READER_SOURCES
    elf_types.cpp
    elf_sections.cpp
)

# ========================================
# 选项：编译成静态库（可被 libso2.so 使用）
# ========================================
option(BUILD_ELF_READER_STATIC "Build elf_reader as static library" ON)

if(BUILD_ELF_READER_STATIC)
    add_library(elf_reader_static STATIC ${ELF_READER_SOURCES})
    target_include_directories(elf_reader_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# ========================================
# 选项：编译成独立可执行文件
# ========================================
option(BUILD_ELF_READER_BINARY "Build elf_reader as standalone binary" ON)

if(BUILD_ELF_READER_BINARY)
    add_executable(elf_reader
        ${ELF_READER_SOURCES}
        main.cpp
    )
    target_include_directories(elf_reader PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    # Android/Linux 下可能需要链接的库
    if(ANDROID)
        # Android 可能需要 log 库
        find_library(log-lib log)
        target_link_libraries(elf_reader ${log-lib})
    endif()
endif()

# ========================================
# 可以添加测试
# ========================================
# TODO: 添加测试目标，使用项目中的 so 文件进行测试