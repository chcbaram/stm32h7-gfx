cmake_minimum_required(VERSION 3.13)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/tools")
include(arm-none-eabi-gcc)

set(PRJ_NAME stm32h7-gfx-ext-loader)


project(${PRJ_NAME}
  LANGUAGES ASM C CXX
)


set(EXECUTABLE ${PRJ_NAME}.stldr)


file(GLOB SRC_FILES CONFIGURE_DEPENDS
  src/*.c
  src/bsp/*.c
  src/bsp/device/*.c
)

file(GLOB_RECURSE SRC_FILES_RECURSE CONFIGURE_DEPENDS
  src/ap/*.cpp
  src/ap/*.c
  src/common/*.c
  src/hw/*.c
  src/lib/STM32H7xx_HAL_Driver/Src/*.c
)

add_executable(${EXECUTABLE} 
  ${SRC_FILES}
  ${SRC_FILES_RECURSE}
)



target_include_directories(${EXECUTABLE} PRIVATE 
  src  
  src/ap
  src/bsp
  src/bsp/device
  src/common 
  src/common/core
  src/common/hw/include 
  src/hw
  src/hw/driver
  src/lib

  src/lib/CMSIS/Include
  src/lib/CMSIS/Device/ST/STM32H7xx/Include
  src/lib/CMSIS/DSP/Include
  src/lib/STM32H7xx_HAL_Driver/Inc
)

target_compile_definitions(${EXECUTABLE} PRIVATE
  -DSTM32H723xx 
  )

target_compile_options(${EXECUTABLE} PRIVATE
  -mcpu=cortex-m7
  -mthumb
  -mfpu=fpv5-d16 
  -mfloat-abi=hard

  # -fdata-sections
  # -ffunction-sections

  -Wall
  # -g3
  -Os
  )

target_link_options(${EXECUTABLE} PRIVATE
  -T../src/bsp/ldscript/STM32H723ZGTX_FLASH.ld
  -mcpu=cortex-m7
  -mthumb
  -mfpu=fpv5-d16 
  -mfloat-abi=hard
  -specs=nano.specs
  # -lc
  # -lm
  # -lstdc++
  # -lsupc++
  # -lnosys

  -nostartfiles
  -nostdlib

  -Wl,-Map=${PRJ_NAME}.map,--cref
  -Wl,--gc-sections
  -Xlinker -print-memory-usage -Xlinker
  )

add_custom_command(TARGET ${EXECUTABLE} 
  POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} ARGS -O binary ${EXECUTABLE} ${PROJECT_NAME}.bin
  COMMENT "Invoking: Make Binary"
  )  