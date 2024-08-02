#include "ltdc.h"


#ifdef _USE_HW_LTDC
#include "sdram.h"
#include "gpio.h"
#include "cli.h"


#define FRAME_BUF_ADDR        HW_LTDC_BUF_ADDR
#define FRAME_OSD_ADDR        (HW_LTDC_BUF_ADDR + (1024+512)*1024)
#define FRAME_BUF_CNT         2


#define LCD_WIDTH             HW_LCD_WIDTH      // LCD PIXEL WIDTH            
#define LCD_HEIGHT            HW_LCD_HEIGHT     // LCD PIXEL HEIGHT           

#if (LCD_MODEL_7_0_800x480_CTP) || (LCD_MODEL_7_0_800x480_RTP)
#define LCD_HSYNC             ((uint16_t)4)     // Horizontal synchronization 
#define LCD_HBP               ((uint16_t)8)     // Horizontal back porch      
#define LCD_HFP               ((uint16_t)8)     // Horizontal front porch     

#define LCD_VSYNC             ((uint16_t)4)     // Vertical synchronization   
#define LCD_VBP               ((uint16_t)16)    // Vertical back porch        
#define LCD_VFP               ((uint16_t)16)    // Vertical front porch       
#else
#define LCD_HSYNC             ((uint16_t)8)     // Horizontal synchronization 
#define LCD_HBP               ((uint16_t)50)    // Horizontal back porch      
#define LCD_HFP               ((uint16_t)10)    // Horizontal front porch     

#define LCD_VSYNC             ((uint16_t)8)     // Vertical synchronization   
#define LCD_VBP               ((uint16_t)20)    // Vertical back porch        
#define LCD_VFP               ((uint16_t)10)    // Vertical front porch       
#endif


#define FRAME_IMG_SIZE        (LCD_WIDTH * LCD_HEIGHT * 2)




void ltdcSetFrameBuffer(uint16_t* addr);
#ifdef _USE_HW_CLI
static void cliCmd(cli_args_t *args);
#endif



static LTDC_HandleTypeDef hltdc;
static bool is_init = false;
static volatile bool ltdc_request_draw = false;

static volatile uint16_t lcd_int_active_line;
static volatile uint16_t lcd_int_porch_line;
static volatile uint32_t frame_rate = 0;
static volatile uint32_t frame_cnt = 0;
static volatile uint32_t frame_time = 0;

static volatile uint32_t  frame_index = 0;
__attribute__((section(".sdram_buf"))) __attribute__((aligned(64)))
static volatile uint16_t frame_mem[LCD_WIDTH*LCD_HEIGHT*2*2];

static uint16_t *frame_buffer[FRAME_BUF_CNT] =
    {
      (uint16_t *)&frame_mem[LCD_WIDTH*LCD_HEIGHT*2*0],
      (uint16_t *)&frame_mem[LCD_WIDTH*LCD_HEIGHT*2*1],
    };

uint16_t *ltdc_draw_buffer;
uint16_t *ltdc_osd_draw_buffer = (uint16_t *)FRAME_OSD_ADDR;

static volatile bool is_double_buffer = true;
static void (*vsync_func)(uint8_t mode) = NULL;





bool ltdcInit(void)
{
  bool ret = true;


  hltdc.Instance = LTDC;

#if (LCD_MODEL_7_0_800x480_CTP) || (LCD_MODEL_7_0_800x480_RTP)
  hltdc.Init.HSPolarity = LTDC_HSPOLARITY_AH;
  hltdc.Init.VSPolarity = LTDC_VSPOLARITY_AH;
  hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AH;
  hltdc.Init.PCPolarity = LTDC_PCPOLARITY_IPC;
#else
  hltdc.Init.HSPolarity = LTDC_HSPOLARITY_AL;
  hltdc.Init.VSPolarity = LTDC_VSPOLARITY_AL;
  hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AL;
  hltdc.Init.PCPolarity = LTDC_PCPOLARITY_IPC;
#endif

  hltdc.Init.HorizontalSync     = (LCD_HSYNC  - 1);
  hltdc.Init.VerticalSync       = (LCD_VSYNC  - 1);
  hltdc.Init.AccumulatedHBP     = (LCD_HSYNC  + LCD_HBP - 1);
  hltdc.Init.AccumulatedVBP     = (LCD_VSYNC  + LCD_VBP - 1);
  hltdc.Init.AccumulatedActiveH = (LCD_HEIGHT + LCD_VSYNC + LCD_VBP - 1);
  hltdc.Init.AccumulatedActiveW = (LCD_WIDTH  + LCD_HSYNC + LCD_HBP - 1);
  hltdc.Init.TotalHeigh         = (LCD_HEIGHT + LCD_VSYNC + LCD_VBP + LCD_VFP - 1);
  hltdc.Init.TotalWidth         = (LCD_WIDTH  + LCD_HSYNC + LCD_HBP + LCD_HFP - 1);

  hltdc.Init.Backcolor.Blue   = 0;
  hltdc.Init.Backcolor.Green  = 0;
  hltdc.Init.Backcolor.Red    = 0;


  if(HAL_LTDC_Init(&hltdc) != HAL_OK)
  {
    ret = false;
  }

  for (int ch=0; ch<FRAME_BUF_CNT; ch++)
  {
    for (int i=0; i<LCD_WIDTH*LCD_HEIGHT; i++)
    {
      frame_buffer[ch][i] = 0x0000;
    }
  }

  ltdcLayerInit(LTDC_LAYER_1, (uint32_t)frame_buffer[frame_index]);
  ltdcLayerInit(LTDC_LAYER_2, FRAME_OSD_ADDR);
  ltdcSetAlpha(LTDC_LAYER_2, 0);


  if (is_double_buffer == true)
  {
    ltdc_draw_buffer = frame_buffer[frame_index ^ 1];
  }
  else
  {
    ltdc_draw_buffer = frame_buffer[frame_index];
  }

  lcd_int_active_line = (LTDC->BPCR & 0x7FF) - (LCD_VBP/2);
  lcd_int_porch_line  = (LTDC->AWCR & 0x7FF) - 1;

  HAL_LTDC_ProgramLineEvent(&hltdc, lcd_int_active_line);
  __HAL_LTDC_ENABLE_IT(&hltdc, LTDC_IT_LI | LTDC_IT_FU);

  NVIC_SetPriority(LTDC_IRQn, 5);
  NVIC_EnableIRQ(LTDC_IRQn);


  ltdcRequestDraw();

  __HAL_RCC_DMA2D_CLK_ENABLE();
  is_init = ret;
  logPrintf("[%s] ltdcInit()\n", is_init ? "OK":"NG");

  PLL3_ClocksTypeDef pll3_clock;
  HAL_RCCEx_GetPLL3ClockFreq(&pll3_clock);
  logPrintf("     freq : %d.%03d Mhz\n", 
    pll3_clock.PLL3_R_Frequency/1000000,
    (pll3_clock.PLL3_R_Frequency%1000000)/1000
    );

#ifdef _USE_HW_CLI
  cliAdd("ltdc", cliCmd);
#endif
  return ret;
}

bool ltdcSetVsyncFunc(void (*func)(uint8_t mode))
{
  vsync_func = func;
  return true;
}

void ltdcSetAlpha(uint16_t LayerIndex, uint32_t value)
{
  HAL_LTDC_SetAlpha(&hltdc, value, LayerIndex);
}


bool ltdcLayerInit(uint16_t LayerIndex, uint32_t Address)
{
  LTDC_LayerCfgTypeDef      pLayerCfg;
  bool ret = true;


  /* Layer1 Configuration ------------------------------------------------------*/

  /* Windowing configuration */
  /* In this case all the active display area is used to display a picture then :
     Horizontal start = horizontal synchronization + Horizontal back porch = 43
     Vertical start   = vertical synchronization + vertical back porch     = 12
     Horizontal stop = Horizontal start + window width -1 = 43 + 480 -1
     Vertical stop   = Vertical start + window height -1  = 12 + 272 -1      */
  if (LayerIndex == LTDC_LAYER_1)
  {
    pLayerCfg.WindowX0 = 0;
    pLayerCfg.WindowX1 = LCD_WIDTH;
    pLayerCfg.WindowY0 = 0;
    pLayerCfg.WindowY1 = LCD_HEIGHT;
  }
  else
  {
    pLayerCfg.WindowX0 = (LCD_WIDTH-200)/2;
    pLayerCfg.WindowX1 = pLayerCfg.WindowX0 + 200;
    pLayerCfg.WindowY0 = (LCD_HEIGHT-200)/2;
    pLayerCfg.WindowY1 = pLayerCfg.WindowY0 + 200;
  }


  /* Pixel Format configuration*/
  pLayerCfg.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;

  /* Start Address configuration : frame buffer is located at FLASH memory */
  pLayerCfg.FBStartAdress = Address;

  /* Alpha constant (255 == totally opaque) */
  pLayerCfg.Alpha = 255;

  /* Default Color configuration (configure A,R,G,B component values) : no background color */
  pLayerCfg.Alpha0          = 0; /* fully transparent */
  pLayerCfg.Backcolor.Blue  = 0;
  pLayerCfg.Backcolor.Green = 0;
  pLayerCfg.Backcolor.Red   = 0;

  /* Configure blending factors */
  pLayerCfg.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
  pLayerCfg.BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;

  /* Configure the number of lines and number of pixels per line */
  pLayerCfg.ImageWidth  = LCD_WIDTH;
  pLayerCfg.ImageHeight = LCD_HEIGHT;


  /* Configure the Layer*/
  if(HAL_LTDC_ConfigLayer(&hltdc, &pLayerCfg, LayerIndex) != HAL_OK)
  {
    /* Initialization Error */
    ret = false;
  }


  return ret;
}


void ltdcSetFrameBuffer(uint16_t* addr)
{
  LTDC_Layer1->CFBAR = (uint32_t)addr;

  /* Reload immediate */
  LTDC->SRCR = (uint32_t)LTDC_SRCR_IMR;
}


int32_t ltdcWidth(void)
{
  return LCD_WIDTH;
}

int32_t ltdcHeight(void)
{
  return LCD_HEIGHT;
}

uint32_t ltdcGetBufferAddr(uint8_t index)
{
  return  (uint32_t)frame_buffer[frame_index];
}

bool ltdcDrawAvailable(void)
{
  return !ltdc_request_draw;
}

void ltdcRequestDraw(void)
{
  ltdc_request_draw = true;
}

void ltdcSetDoubleBuffer(bool enable)
{
  is_double_buffer = enable;

  if (enable == true)
  {
    ltdc_draw_buffer = frame_buffer[frame_index^1];
  }
  else
  {
    ltdc_draw_buffer = frame_buffer[frame_index];
  }
}

bool ltdcGetDoubleBuffer(void)
{
  return is_double_buffer;
}

uint16_t *ltdcGetFrameBuffer(void)
{
  return  ltdc_draw_buffer;
}

uint16_t *ltdcGetCurrentFrameBuffer(void)
{
  return  frame_buffer[frame_index];
}


void ltdcSwapFrameBuffer(void)
{
  if (ltdc_request_draw == true)
  {
    frame_index ^= 1;
    ltdcSetFrameBuffer(frame_buffer[frame_index]);

    if (is_double_buffer == true)
    {
      ltdc_draw_buffer = frame_buffer[frame_index ^ 1];
    }
    else
    {
      ltdc_draw_buffer = frame_buffer[frame_index];
    }
    ltdc_request_draw = false;
  }
}

void LTDC_IRQHandler(void)
{
  HAL_LTDC_IRQHandler(&hltdc);
}

void HAL_LTDC_LineEvenCallback(LTDC_HandleTypeDef* hltdc)
{
  if (LTDC->LIPCR == lcd_int_active_line)
  {
    ltdcSwapFrameBuffer();
    HAL_LTDC_ProgramLineEvent(hltdc, lcd_int_active_line);

    if (vsync_func != NULL)
    {
      vsync_func(0);
    }
    frame_cnt++;
    if (millis()-frame_time >= 1000)
    {
      frame_time = millis();
      frame_rate = frame_cnt;
      frame_cnt = 0;
    }
  }
  else
  {
    HAL_LTDC_ProgramLineEvent(hltdc, lcd_int_active_line);
    if (vsync_func != NULL)
    {
      vsync_func(1);
    }    
  }
}



void HAL_LTDC_MspInit(LTDC_HandleTypeDef* ltdcHandle)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  if(ltdcHandle->Instance==LTDC)
  {
  /** Initializes the peripherals clock
  */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_LTDC;
    PeriphClkInitStruct.PLL3.PLL3M = 10;
    PeriphClkInitStruct.PLL3.PLL3N = 224;
    PeriphClkInitStruct.PLL3.PLL3P = 11;
    PeriphClkInitStruct.PLL3.PLL3Q = 2;
#if (LCD_MODEL_7_0_800x480_CTP) || (LCD_MODEL_7_0_800x480_RTP)
    PeriphClkInitStruct.PLL3.PLL3R = 22;
#else
    PeriphClkInitStruct.PLL3.PLL3R = 34;
#endif
    PeriphClkInitStruct.PLL3.PLL3RGE = RCC_PLL3VCIRANGE_1;
    PeriphClkInitStruct.PLL3.PLL3VCOSEL = RCC_PLL3VCOWIDE;
    PeriphClkInitStruct.PLL3.PLL3FRACN = 0;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    /* LTDC clock enable */
    __HAL_RCC_LTDC_CLK_ENABLE();

    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    /**LTDC GPIO Configuration
    PF10     ------> LTDC_DE
    PA5     ------> LTDC_R4
    PA6     ------> LTDC_G2
    PA7     ------> LTDC_VSYNC
    PB0     ------> LTDC_R3
    PB1     ------> LTDC_R6
    PB10     ------> LTDC_G4
    PB11     ------> LTDC_G5
    PG6     ------> LTDC_R7
    PG7     ------> LTDC_CLK
    PC6     ------> LTDC_HSYNC
    PC7     ------> LTDC_G6
    PC9     ------> LTDC_G3
    PA8     ------> LTDC_B3
    PA9     ------> LTDC_R5
    PA10     ------> LTDC_B4
    PA15(JTDI)     ------> LTDC_B6
    PD2     ------> LTDC_B7
    PD3     ------> LTDC_G7
    PB5     ------> LTDC_B5
    */
    GPIO_InitStruct.Pin = GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF14_LTDC;
    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_9
                          |GPIO_PIN_15;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF14_LTDC;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF9_LTDC;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_10|GPIO_PIN_11;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF14_LTDC;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF14_LTDC;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF14_LTDC;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF10_LTDC;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_8;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF13_LTDC;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF12_LTDC;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_2;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF9_LTDC;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF14_LTDC;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF3_LTDC;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
  }
}

void HAL_LTDC_MspDeInit(LTDC_HandleTypeDef *hltdc)
{
}


#ifdef _USE_HW_CLI
void cliCmd(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info") == true)
  {
    cliPrintf("is_init    : %s\n", is_init ? "True":"False");
    cliPrintf("buf addr 0 : 0x%X\n", (uint32_t)frame_buffer[0]);
    cliPrintf("         1 : 0x%X\n", (uint32_t)frame_buffer[1]);
    cliPrintf("frame rate : %d fps\n", frame_rate);
    cliPrintf("int active : %d \n", lcd_int_active_line);
    cliPrintf("int porch  : %d \n", lcd_int_porch_line);
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "test") == true)
  {
    uint32_t pre_time;
    uint16_t color_tbl[] = {0xF800, 0x07E0, 0x001F};
    uint16_t color_index = 0;


    pre_time = millis();
    while(cliKeepLoop())
    {
      if (ltdcDrawAvailable() && millis()-pre_time >= 500)
      {
        pre_time = millis();
        
        uint16_t *p_buf;
        uint32_t pre_time_draw = millis();
        uint32_t exe_time;
        uint16_t color;

        p_buf = ltdcGetFrameBuffer();
        color = color_tbl[color_index];
        for (int i=0; i<LCD_WIDTH*LCD_HEIGHT; i++)
        {
          p_buf[i] = color;
        }
        exe_time = millis()-pre_time_draw;
        
        ltdcRequestDraw();

        cliPrintf("%d ms, color 0x%04X\n", exe_time, color);

        color_index = (color_index + 1) % (sizeof(color_tbl)/sizeof(uint16_t));
      }
    }
    ret = true;
  }

  if (ret != true)
  {
    cliPrintf("ltdc info\n");
    cliPrintf("ltdc test\n");
  }
}
#endif

#endif