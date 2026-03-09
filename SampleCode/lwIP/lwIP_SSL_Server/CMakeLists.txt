cmake_minimum_required(VERSION 3.20)

project(lwIP_SSL_Server LANGUAGES C ASM)

set(CMAKE_EXECUTABLE_SUFFIX ".elf")

set(BSP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
set(ARCH_ROOT "${BSP_ROOT}/Library/Arch/Core_A")
set(DEVICE_ROOT "${BSP_ROOT}/Library/Device/Nuvoton/MA35D1")
set(STDDRIVER_ROOT "${BSP_ROOT}/Library/StdDriver")
set(FREERTOS_ROOT "${BSP_ROOT}/ThirdParty/FreeRTOS-Kernel")
set(LWIP_ROOT "${BSP_ROOT}/ThirdParty/lwIP/src")
set(LWIP_API_ROOT "${LWIP_ROOT}/api")
set(LWIP_CORE_ROOT "${LWIP_ROOT}/core")
set(LWIP_NETIF_ROOT "${LWIP_ROOT}/netif")
set(LWIP_PORT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../port")
set(MBEDTLS_ROOT "${BSP_ROOT}/ThirdParty/mbedtls-3.1.0")

set(CORE_A_SOURCES
    "${ARCH_ROOT}/Source/cache.c"
    "${ARCH_ROOT}/Source/cpu.S"
    "${ARCH_ROOT}/Source/irq_ctrl_gic.c"
    "${ARCH_ROOT}/Source/mmu.c"
    "${ARCH_ROOT}/Source/vectors.c"
    "${ARCH_ROOT}/Source/vector_sram.S"
)

set(STDDRIVER_SOURCES
    "${STDDRIVER_ROOT}/src/clk.c"
    "${STDDRIVER_ROOT}/src/gmac.c"
    "${STDDRIVER_ROOT}/src/pmic.c"
    "${STDDRIVER_ROOT}/src/retarget.c"
    "${STDDRIVER_ROOT}/src/ssmcc.c"
    "${STDDRIVER_ROOT}/src/sys.c"
    "${STDDRIVER_ROOT}/src/timer.c"
    "${STDDRIVER_ROOT}/src/uart.c"
)

set(FREERTOS_SOURCES
    "${FREERTOS_ROOT}/event_groups.c"
    "${FREERTOS_ROOT}/list.c"
    "${FREERTOS_ROOT}/queue.c"
    "${FREERTOS_ROOT}/stream_buffer.c"
    "${FREERTOS_ROOT}/tasks.c"
    "${FREERTOS_ROOT}/timers.c"
    "${FREERTOS_ROOT}/portable/GCC/ARM_CA35_64_BIT/port.c"
    "${FREERTOS_ROOT}/portable/GCC/ARM_CA35_64_BIT/portASM.S"
    "${FREERTOS_ROOT}/portable/GCC/ARM_CA35_64_BIT/portmacro.h"
    "${FREERTOS_ROOT}/portable/MemMang/heap_4.c"
)

file(GLOB_RECURSE LWIP_API_SOURCE 
     LIST_DIRECTORIES false 
     "${LWIP_API_ROOT}/*.c"
)

file(GLOB_RECURSE LWIP_CORE_SOURCE 
     LIST_DIRECTORIES false 
     "${LWIP_CORE_ROOT}/*.c"
)

file(GLOB_RECURSE LWIP_NETIF_SOURCE 
     LIST_DIRECTORIES false 
     "${LWIP_NETIF_ROOT}/*.c"
)

set(LWIP_SOURCES
    ${LWIP_API_SOURCE}
    ${LWIP_CORE_SOURCE}
    ${LWIP_NETIF_SOURCE}
    "${LWIP_PORT_ROOT}/netif/ethernetif.c"
    "${LWIP_PORT_ROOT}/netif/ma35d1_mac.c"
    "${LWIP_PORT_ROOT}/sys_arch.c"
)

file(GLOB_RECURSE MBEDTLS_SOURCES 
     LIST_DIRECTORIES false 
     "${MBEDTLS_ROOT}/library/*.c"
)
list(REMOVE_ITEM MBEDTLS_SOURCES "${MBEDTLS_ROOT}/library/net_sockets.c")

set(MBEDTLS_SOURCES
    ${MBEDTLS_SOURCES}
    "${MBEDTLS_ROOT}/tests/src/certs.c"
)

set(PROJECT_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/FreeRTOS_tick_config.c"
    "${CMAKE_CURRENT_LIST_DIR}/main.c"
    "${CMAKE_CURRENT_LIST_DIR}/net_sockets.c"
    "${CMAKE_CURRENT_LIST_DIR}/ssl_server.c"
    "${DEVICE_ROOT}/Source/system_MA35D1.c"
    "${DEVICE_ROOT}/Source/mmu_MA35D1.c"
    "${FREERTOS_ROOT}/portable/GCC/ARM_CA35_64_BIT/FreeRTOS_startup.S"
    ${CORE_A_SOURCES}
    ${STDDRIVER_SOURCES}
    ${FREERTOS_SOURCES}
    ${LWIP_SOURCES}
    ${MBEDTLS_SOURCES}
)

add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${ARCH_ROOT}/Include"
    "${DEVICE_ROOT}/Include"
    "${STDDRIVER_ROOT}/inc"
    "${FREERTOS_ROOT}/include"
    "${FREERTOS_ROOT}/portable/GCC/ARM_CA35_64_BIT"
    "${FREERTOS_ROOT}/common/include"
    "${LWIP_ROOT}/include"
    "${LWIP_PORT_ROOT}/include"
    "${MBEDTLS_ROOT}/include"
    "${MBEDTLS_ROOT}/tests/include"
    "${MBEDTLS_ROOT}/library"
)

target_compile_options(${PROJECT_NAME} PRIVATE
    -mcpu=cortex-a35
    -ffunction-sections
    -fdata-sections
    -fmessage-length=0
    -fsigned-char
)

target_compile_definitions(${PROJECT_NAME} PRIVATE 
    MBEDTLS_CONFIG_FILE="mbedtls_config.h"
)

set(LINKER_SCRIPT "${DEVICE_ROOT}/Source/GCC/gcc_arm.ld")

target_link_options(${PROJECT_NAME} PRIVATE
    -mcpu=cortex-a35
    -nostartfiles
    --specs=rdimon.specs
    "-T${LINKER_SCRIPT}"
    -Wl,--gc-sections
    "-Wl,-Map,$<TARGET_FILE_DIR:${PROJECT_NAME}>/${PROJECT_NAME}.map"
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND "${CMAKE_OBJCOPY}" -O binary "$<TARGET_FILE:${PROJECT_NAME}>" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/${PROJECT_NAME}.bin"
    COMMAND "${CMAKE_OBJDUMP}" -h -S "$<TARGET_FILE:${PROJECT_NAME}>" > "$<TARGET_FILE_DIR:${PROJECT_NAME}>/${PROJECT_NAME}.lst"
    COMMAND "${CMAKE_SIZE}" "$<TARGET_FILE:${PROJECT_NAME}>"
    COMMENT "Generating .bin/.lst and printing size"
)
