cmake_minimum_required(VERSION 3.22)

# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

include("nucleo_f446re_base/cmake/gcc-arm-none-eabi.cmake")

# The toolchain file sets the linker script path relative to CMAKE_SOURCE_DIR,
# which is this directory. Fix it to point into nucleo_f446re_base/.
string(REPLACE
    "${CMAKE_SOURCE_DIR}/STM32F446RETx_FLASH.ld"
    "${CMAKE_SOURCE_DIR}/nucleo_f446re_base/STM32F446RETx_FLASH.ld"
    CMAKE_C_LINK_FLAGS
    "${CMAKE_C_LINK_FLAGS}"
)

enable_language(C ASM)
project(validation_firmware)

# Include only the stm32cubemx interface library (bypass base CMakeLists.txt
# which would re-run toolchain detection).
add_subdirectory(nucleo_f446re_base/cmake/stm32cubemx stm32cubemx_build)

add_executable(simple
    simple/firmware.c
)
target_link_libraries(simple stm32cubemx)
