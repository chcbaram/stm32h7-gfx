cmake_minimum_required(VERSION 3.13)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/tools")
include(arm-none-eabi-gcc)

set(PRJ_NAME stm32h7-gfx-fw)


project(${PRJ_NAME}
  LANGUAGES ASM C CXX
)


set(EXECUTABLE ${PRJ_NAME}.elf)


file(GLOB SRC_FILES CONFIGURE_DEPENDS
  src/*.cpp
  src/*.c
  src/bsp/*.c
  src/bsp/device/*.c
  src/bsp/rtos/*.c

  src/lib/FatFs/src/*.c
  src/lib/FatFs/src/option/syscall.c
  src/lib/FatFs/src/option/unicode.c

  src/lib/STM32_USB_Device_Library/Core/Src/*.c

  src/lib/FreeRTOS/Source/*.c
  src/lib/FreeRTOS/Source/portable/GCC/ARM_CM7/r0p1/*.c
  src/lib/FreeRTOS/Source/CMSIS_RTOS/*.c  

  src/lib/FreeRTOS/Source/portable/MemMang/heap_4.c

  src/lib/CMSIS/DSP/Source/FastMathFunctions/arm_cos_f32.c
  src/lib/CMSIS/DSP/Source/FastMathFunctions/arm_sin_f32.c
  src/lib/CMSIS/DSP/Source/SupportFunctions/arm_q15_to_float.c
  src/lib/CMSIS/DSP/Source/SupportFunctions/arm_fill_q15.c
  src/lib/CMSIS/DSP/Source/TransformFunctions/arm_rfft_fast_f32.c  
  src/lib/CMSIS/DSP/Source/TransformFunctions/arm_rfft_fast_init_f32.c
  src/lib/CMSIS/DSP/Source/ComplexMathFunctions/arm_cmplx_dot_prod_f32.c
  src/lib/CMSIS/DSP/Source/ComplexMathFunctions/arm_cmplx_mag_f32.c
  src/lib/CMSIS/DSP/Source/StatisticsFunctions/arm_power_q15.c
)

file(GLOB_RECURSE SRC_FILES_RECURSE CONFIGURE_DEPENDS
  src/ap/*.cpp
  src/ap/*.c
  src/bsp/*.s
  src/common/*.c
  src/hw/*.c
  src/lib/STM32H7xx_HAL_Driver/Src/*.c
  src/lib/littlefs/*.c

  src/ap/touch-gfx/*.cpp
  src/lib/touchgfx/framework/source/touchgfx/*.cpp  
)

# 특정 폴더를 빌드에서 제외한다.
#
set(EXCLUDE_PATHS 
  "src/ap/touch-gfx/build"
  "src/ap/touch-gfx/simulator"
  "src/ap/touch-gfx/generated/simulator"
)

foreach(TMP_PATH ${SRC_FILES_RECURSE}) 
  foreach(EXCLUDE_PATH ${EXCLUDE_PATHS}) 
    string(FIND ${TMP_PATH} ${EXCLUDE_PATH} RESULT) 
    if(NOT ${RESULT} EQUAL -1) 
      list(REMOVE_ITEM SRC_FILES_RECURSE ${TMP_PATH}) 
    endif() 
  endforeach(EXCLUDE_PATH) 
endforeach(TMP_PATH)


add_executable(${EXECUTABLE} 
  ${SRC_FILES}
  ${SRC_FILES_RECURSE}
)



target_include_directories(${EXECUTABLE} PRIVATE 
  src  
  src/ap
  src/ap/thread
  src/bsp
  src/bsp/device
  src/bsp/rtos
  src/common 
  src/common/core
  src/common/hw/include 
  src/hw
  src/hw/driver
  src/hw/driver/fatfs
  src/hw/driver/usb
  src/hw/driver/usb/usb_cdc
  src/hw/driver/usb/usb_msc
  src/lib

  src/lib/CMSIS/Include
  src/lib/CMSIS/Device/ST/STM32H7xx/Include
  src/lib/CMSIS/DSP/Include
  src/lib/STM32H7xx_HAL_Driver/Inc
  src/lib/FatFs/src
  src/lib/STM32_USB_Device_Library/Core/Inc

  src/lib/FreeRTOS/Source/CMSIS_RTOS
  src/lib/FreeRTOS/Source/include
  src/lib/FreeRTOS/Source/portable/GCC/ARM_CM7/r0p1  

  src/lib/STM32_Audio/Addons/HP_PDM/Inc
  src/lib/STM32_AcousticSL_Library/Inc

  src/lib/picovoice/include

  src/ap/touch-gfx
  src/ap/touch-gfx/target
  src/ap/touch-gfx/target/generated    
  src/ap/touch-gfx/gui/include
  src/ap/touch-gfx/generated/gui_generated/include
  src/ap/touch-gfx/generated/fonts/include
  src/ap/touch-gfx/generated/texts/include
  src/ap/touch-gfx/generated/images/include
  src/lib/touchgfx/framework/include  
)

target_compile_definitions(${EXECUTABLE} PRIVATE
  -DSTM32H723xx 
  -DLFS_THREADSAFE
  -D__PV_LANGUAGE_KOREAN__
  )

target_compile_options(${EXECUTABLE} PRIVATE
  -mcpu=cortex-m7
  -mthumb
  -mfpu=fpv5-d16 
  -mfloat-abi=hard

  -fdata-sections
  -ffunction-sections

  -Wall
  -g3
  -O0
  )

target_link_options(${EXECUTABLE} PRIVATE
  -T../src/bsp/ldscript/STM32H723ZGTX_FLASH.ld
  # -T../src/bsp/ldscript/STM32H723ZGTX_BOOT.ld
  -mcpu=cortex-m7
  -mthumb
  -mfpu=fpv5-d16 
  -mfloat-abi=hard
  -specs=nano.specs
  -lc
  -lm
  -lstdc++
  -lsupc++
  -fno-rtti
  #-lnosys
  -Wl,-Map=${PRJ_NAME}.map,--cref
  -Wl,--gc-sections
  -Xlinker -print-memory-usage -Xlinker
  )


set (CMAKE_CXX_FLAGS "-fno-rtti")

target_link_libraries( ${EXECUTABLE} PRIVATE
  ${CMAKE_SOURCE_DIR}/src/lib/CMSIS/DSP/Lib/GCC/libarm_cortexM7lfdp_math.a 
  ${CMAKE_SOURCE_DIR}/src/lib/STM32_Audio/Addons/HP_PDM/Lib/libPDMFilter_CM7_GCC_wc32.a
  ${CMAKE_SOURCE_DIR}/src/lib/STM32_AcousticSL_Library/Lib/libAcousticSL_CM7F_wc32_ot.a
  ${CMAKE_SOURCE_DIR}/src/lib/picovoice/ko/libpicovoice.a
  ${CMAKE_SOURCE_DIR}/src/lib/touchgfx/lib/core/cortex_m7/gcc/libtouchgfx-float-abi-hard.a
  )

add_custom_command(TARGET ${EXECUTABLE} 
  POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} ARGS -O binary ${EXECUTABLE} ${PROJECT_NAME}.bin
  COMMENT "Invoking: Make Binary"
  )  