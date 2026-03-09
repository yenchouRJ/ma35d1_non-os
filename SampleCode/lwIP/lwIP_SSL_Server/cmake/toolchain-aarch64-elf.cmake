set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Fallback to your specific default only if nothing else is provided
if(NOT AARCH64_ELF_TOOLCHAIN_DIR)
    set(AARCH64_ELF_TOOLCHAIN_DIR "PATH/TO/gcc-arm-8.3-2019.03-i686-mingw32-aarch64-elf")
endif()

set(_tool_bin "${AARCH64_ELF_TOOLCHAIN_DIR}/bin")
set(CROSS_COMPILE "aarch64-elf-")

set(CMAKE_C_COMPILER   "${_tool_bin}/${CROSS_COMPILE}gcc.exe")
set(CMAKE_ASM_COMPILER "${_tool_bin}/${CROSS_COMPILE}gcc.exe")
set(CMAKE_OBJCOPY      "${_tool_bin}/${CROSS_COMPILE}objcopy.exe")
set(CMAKE_OBJDUMP      "${_tool_bin}/${CROSS_COMPILE}objdump.exe")
set(CMAKE_SIZE         "${_tool_bin}/${CROSS_COMPILE}size.exe")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)