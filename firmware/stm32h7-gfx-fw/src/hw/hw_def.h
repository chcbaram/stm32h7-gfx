#ifndef HW_DEF_H_
#define HW_DEF_H_



#include "bsp.h"


#define _DEF_FIRMWATRE_VERSION    "V230811R1"
#define _DEF_BOARD_NAME           "STM32H7-GFX-FW"



#define _USE_HW_FAULT
#define _USE_HW_QSPI
#define _USE_HW_FLASH
#define _USE_HW_FILES
#define _USE_HW_NVS
#define _USE_HW_SD
#define _USE_HW_FATFS
#define _USE_HW_CACHE
#define _USE_HW_BUZZER
#define _USE_HW_GT911
#define _USE_HW_RTOS


#define _USE_HW_LED
#define      HW_LED_MAX_CH          1

#define _USE_HW_UART
#define      HW_UART_MAX_CH         2
#define      HW_UART_CH_SWD         _DEF_UART1
#define      HW_UART_CH_USB         _DEF_UART2

#define _USE_HW_CLI
#define      HW_CLI_CMD_LIST_MAX    32
#define      HW_CLI_CMD_NAME_MAX    16
#define      HW_CLI_LINE_HIS_MAX    8
#define      HW_CLI_LINE_BUF_MAX    64

#define _USE_HW_CLI_GUI
#define      HW_CLI_GUI_WIDTH       80
#define      HW_CLI_GUI_HEIGHT      24

#define _USE_HW_LOG
#define      HW_LOG_CH              _DEF_UART1
#define      HW_LOG_BOOT_BUF_MAX    1024
#define      HW_LOG_LIST_BUF_MAX    1024

#define _USE_HW_GPIO
#define      HW_GPIO_MAX_CH         8

#define _USE_HW_BUTTON
#define      HW_BUTTON_MAX_CH       1

#define _USE_HW_SWTIMER
#define      HW_SWTIMER_MAX_CH      8

#define _USE_HW_RESET
#define      HW_RESET_BOOT          1

#define _USE_HW_RTC
#define      HW_RTC_BOOT_MODE       RTC_BKP_DR3
#define      HW_RTC_RESET_BITS      RTC_BKP_DR4

#define _USE_HW_I2C
#define      HW_I2C_MAX_CH          2
#define      HW_I2C_CH_EEPROM       _DEF_I2C1
#define      HW_I2C_CH_TOUCH        _DEF_I2C2

#define _USE_HW_USB
#define _USE_HW_CDC
#define      HW_USE_CDC             1
#define      HW_USE_MSC             0

#define _USE_HW_I2S
#define      HW_I2S_LCD             1

#define _USE_HW_MIXER
#define      HW_MIXER_MAX_CH        4
#define      HW_MIXER_MAX_BUF_LEN   (48*2*4*4) // 48Khz * Stereo * 4ms * 2

#define _USE_HW_SPI
#define      HW_SPI_MAX_CH          2

#define _USE_HW_SPI_FLASH
#define      HW_SPI_FLASH_ADDR      0x91000000

#define _USE_HW_FS
#define      HW_FS_FLASH_OFFSET     0x91000000
#define      HW_FS_MAX_SIZE         (8*1024*1024)

#define _USE_HW_EEPROM
#define      HW_EEPROM_MAX_SIZE     (8*1024)

#define _USE_HW_SDRAM
#define      HW_SDRAM_MEM_ADDR      0xC0000000
#define      HW_SDRAM_MEM_SIZE      (32*1024*1024)

#define _USE_HW_TOUCH
#define      HW_TOUCH_MAX_CH        5

#define _USE_HW_PWM
#define      HW_PWM_MAX_CH          1

#define _USE_HW_ST7701
#define      HW_ST7701_WIDTH       480
#define      HW_ST7701_HEIGHT      480

#define _USE_HW_LTDC
#define      HW_LTDC_BUF_ADDR      HW_SDRAM_MEM_ADDR

#define _USE_HW_LCD
#define      HW_LCD_LOGO            1
#define      HW_LCD_LVGL            1
#define      HW_LCD_WIDTH           HW_ST7701_WIDTH
#define      HW_LCD_HEIGHT          HW_ST7701_HEIGHT

#define _USE_HW_PDM
#define      HW_PDM_MIC_MAX_CH      2

#define _USE_HW_MEM
#define      HW_MEM_BUF_ADDR        (HW_SDRAM_MEM_ADDR + 8*1024*1024)
#define      HW_MEM_BUF_SIZE        (8*1024*1024)


#define _PIN_GPIO_SPI_FLASH_CS      0
#define _PIN_GPIO_SDCARD_DETECT     1
#define _PIN_GPIO_SPK_EN            2
#define _PIN_GPIO_LCD_TS_RST        3
#define _PIN_GPIO_LCD_TS_INT        4
#define _PIN_GPIO_LCD_BLK           5
#define _PIN_GPIO_LCD_RST           6
#define _PIN_GPIO_LCD_SPI_CS        7

#define FLASH_SIZE_TAG              0x400
#define FLASH_SIZE_VER              0x400
#define FLASH_SIZE_FIRM             (1024*1024 - 128*1024)

#define FLASH_ADDR_BOOT             0x08000000
#define FLASH_ADDR_FIRM             0x08020000

#define FLASH_ADDR_UPDATE           0x90800000



typedef struct thread_t_
{
  const char    *name;
  bool         (*init)(void); 
  void         (*main)(void const *arg);
  osPriority    priority;
  uint32_t      stack_size;
  osThreadDef_t thread_def;
  osThreadId    thread_id;
} thread_t;

#endif