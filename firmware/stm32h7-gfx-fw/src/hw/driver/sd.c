/*
 * sd.c
 *
 *  Created on: 2020. 12. 25.
 *      Author: baram
 */


#include "sd.h"
#include "gpio.h"
#include "cli.h"


#ifdef _USE_HW_SD





static bool is_init = false;
static bool is_detected = false;
static volatile bool is_rx_done = false;
static volatile bool is_tx_done = false;
static uint8_t is_try = 0;
static sd_state_t sd_state = SDCARD_IDLE;


#if HW_SD_EMMC
static MMC_HandleTypeDef hmmc;
#else
static SD_HandleTypeDef hsd;
#endif


#ifdef _USE_HW_CLI
static void cliSd(cli_args_t *args);
#endif





bool sdInit(void)
{
  bool ret = false;
  #if HW_SD_EMMC
  MMC_HandleTypeDef *p_sd = &hmmc;
  #else
  SD_HandleTypeDef *p_sd = &hsd;
  #endif
 

  // 200Mhz / (2*2) = 50Mhz
  p_sd->Instance            = SDMMC2;
  p_sd->Init.ClockEdge      = SDMMC_CLOCK_EDGE_RISING;
  p_sd->Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
  p_sd->Init.BusWide        = SDMMC_BUS_WIDE_4B;
  p_sd->Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
  p_sd->Init.ClockDiv       = SDMMC_HSPEED_CLK_DIV;

  is_detected = sdIsDetected();

  if (is_detected == true)
  {
    #if HW_SD_EMMC
    HAL_MMC_DeInit(&hmmc);
    if (HAL_MMC_Init(&hmmc) == HAL_OK)
    #else
    if (HAL_SD_Init(&hsd) == HAL_OK)
    #endif
    {
      ret = true;
    }
  }

  is_init = ret;


  static bool is_reinit = false;

  if (is_reinit == false)
  {
    logPrintf("[%s] sdInit()\n", ret ? "OK":"NG");   
    if (is_detected == false)
      logPrintf("     sdcard not found\n");   
  }
    

#ifdef _USE_HW_CLI
  if (is_reinit == false)
    cliAdd("sd", cliSd);
#endif

  is_reinit = true;
  
  return ret;
}

bool sdReInit(void)
{
  bool ret = false;

  #if HW_SD_EMMC
  HAL_MMC_DeInit(&hmmc);
  if (HAL_MMC_Init(&hmmc) == HAL_OK)
  #else
  HAL_SD_DeInit(&hsd);
  if (HAL_SD_Init(&hsd) == HAL_OK)
  #endif
  {
    ret = true;
  }

  is_init = ret;

  return ret;
}

bool sdDeInit(void)
{
  bool ret = false;

  if (is_init == true)
  {
    is_init = false;
    #if HW_SD_EMMC
    if (HAL_MMC_DeInit(&hmmc) == HAL_OK)
    #else 
    if (HAL_SD_DeInit(&hsd) == HAL_OK)
    #endif
    {
      ret = true;
    }
  }

  return ret;
}

bool sdIsInit(void)
{
  return is_init;
}

bool sdIsDetected(void)
{
  if (gpioPinRead(_PIN_GPIO_SDCARD_DETECT) == _DEF_LOW)
  {
    is_detected = true;
  }
  else
  {
    is_detected = false;
  }

  return is_detected;
}

sd_state_t sdUpdate(void)
{
  sd_state_t ret_state = SDCARD_IDLE;
  static uint32_t pre_time;


  switch(sd_state)
  {
    case SDCARD_IDLE:
      if (sdIsDetected() == true)
      {
        if (is_init)
        {
          sd_state = SDCARD_CONNECTED;
        }
        else
        {
          sd_state = SDCARD_CONNECTTING;
          pre_time = millis();
        }
      }
      else
      {
        is_init = false;
        sd_state  = SDCARD_DISCONNECTED;
        ret_state = SDCARD_DISCONNECTED;
      }
      break;

    case SDCARD_CONNECTTING:
      if (millis()-pre_time >= 100)
      {
        if (sdReInit())
        {
          sd_state  = SDCARD_CONNECTED;
          ret_state = SDCARD_CONNECTED;
        }

        else
        {
          sd_state = SDCARD_IDLE;
          is_try++;

          if (is_try >= 3)
          {
            sd_state = SDCARD_ERROR;
          }
        }
      }
      break;

    case SDCARD_CONNECTED:
      if (sdIsDetected() != true)
      {
        is_try = 0;
        sd_state = SDCARD_IDLE;
      }
      break;

    case SDCARD_DISCONNECTED:
      if (sdIsDetected() == true)
      {
        sd_state = SDCARD_IDLE;
      }
      break;

    case SDCARD_ERROR:
      break;
  }

  return ret_state;
}

bool sdGetInfo(sd_info_t *p_info)
{
  bool ret = false;
  sd_info_t *p_sd_info = (sd_info_t *)p_info;

  #if HW_SD_EMMC
  HAL_MMC_CardInfoTypeDef card_info;


  if (is_init == true)
  {
    HAL_MMC_GetCardInfo(&hmmc, &card_info);

    p_sd_info->card_type          = card_info.CardType;
    p_sd_info->card_version       = 0;
    p_sd_info->card_class         = card_info.Class;
    p_sd_info->rel_card_Add       = card_info.RelCardAdd;
    p_sd_info->block_numbers      = card_info.BlockNbr;
    p_sd_info->block_size         = card_info.BlockSize;
    p_sd_info->log_block_numbers  = card_info.LogBlockNbr;
    p_sd_info->log_block_size     = card_info.LogBlockSize;
    p_sd_info->card_size          =  (uint32_t)((uint64_t)p_sd_info->block_numbers * (uint64_t)p_sd_info->block_size / (uint64_t)1024 / (uint64_t)1024);
    ret = true;
  }
  #else
  HAL_SD_CardInfoTypeDef card_info;


  if (is_init == true)
  {
    HAL_SD_GetCardInfo(&hsd, &card_info);

    p_sd_info->card_type          = card_info.CardType;
    p_sd_info->card_version       = card_info.CardVersion;
    p_sd_info->card_class         = card_info.Class;
    p_sd_info->rel_card_Add       = card_info.RelCardAdd;
    p_sd_info->block_numbers      = card_info.BlockNbr;
    p_sd_info->block_size         = card_info.BlockSize;
    p_sd_info->log_block_numbers  = card_info.LogBlockNbr;
    p_sd_info->log_block_size     = card_info.LogBlockSize;
    p_sd_info->card_size          =  (uint32_t)((uint64_t)p_sd_info->block_numbers * (uint64_t)p_sd_info->block_size / (uint64_t)1024 / (uint64_t)1024);
    ret = true;
  }
  #endif

  return ret;
}

bool sdIsBusy(void)
{
  bool is_busy;

  #if HW_SD_EMMC
  if (HAL_MMC_GetCardState(&hmmc) == HAL_SD_CARD_TRANSFER )
  #else
  if (HAL_SD_GetCardState(&hsd) == HAL_SD_CARD_TRANSFER )
  #endif
  {
    is_busy = false;
  }
  else
  {
    is_busy = true;
  }

  return is_busy;
}

bool sdIsReady(uint32_t timeout)
{
  uint32_t pre_time;

  pre_time = millis();

  while(millis() - pre_time < timeout)
  {
    if (sdIsBusy() == false)
    {
      return true;
    }
  }

  return false;
}

bool sdReadBlocks(uint32_t block_addr, uint8_t *p_data, uint32_t num_of_blocks, uint32_t timeout_ms)
{
  bool ret = false;
  uint32_t pre_time;


  if (is_init == false) return false;


  is_rx_done = false;
  #if HW_SD_EMMC
  if(HAL_MMC_ReadBlocks_DMA(&hmmc, (uint8_t *)p_data, block_addr, num_of_blocks) == HAL_OK)
  #else
  if(HAL_SD_ReadBlocks_DMA(&hsd, (uint8_t *)p_data, block_addr, num_of_blocks) == HAL_OK)
  #endif
  {

    pre_time = millis();
    while(is_rx_done == false)
    {
      if (millis()-pre_time >= timeout_ms)
      {
        break;
      }
    }
    while(sdIsBusy() == true)
    {
      if (millis()-pre_time >= timeout_ms)
      {
        is_rx_done = false;
        break;
      }
    }
    ret = is_rx_done;
  }
  if (ret == true)
  {
    #ifdef _USE_HW_CACHE
    SCB_InvalidateDCache_by_Addr((uint32_t*)p_data, BLOCKSIZE * num_of_blocks);
    #endif
  }
  return ret;
}

bool sdWriteBlocks(uint32_t block_addr, uint8_t *p_data, uint32_t num_of_blocks, uint32_t timeout_ms)
{
  bool ret = false;
  uint32_t pre_time;

  if (is_init == false) return false;


  #ifdef _USE_HW_CACHE
  SCB_InvalidateDCache_by_Addr((uint32_t *)p_data, num_of_blocks * BLOCKSIZE);  
  #endif

  is_tx_done = false;
  #if HW_SD_EMMC
  if(HAL_MMC_WriteBlocks_DMA(&hmmc, (uint8_t *)p_data, block_addr, num_of_blocks) == HAL_OK)
  #else
  if(HAL_SD_WriteBlocks_DMA(&hsd, (uint8_t *)p_data, block_addr, num_of_blocks) == HAL_OK)
  #endif
  {
    pre_time = millis();
    while(is_tx_done == false)
    {
      if (millis()-pre_time >= timeout_ms)
      {
        break;
      }
    }
    pre_time = millis();
    while(sdIsBusy() == true)
    {
      if (millis()-pre_time >= timeout_ms)
      {
        is_tx_done = false;
        break;
      }
    }
    ret = is_tx_done;
  }

  return ret;
}

bool sdEraseBlocks(uint32_t start_addr, uint32_t end_addr)
{
  bool ret = false;

  if (is_init == false) return false;

  #if HW_SD_EMMC
  if(HAL_MMC_Erase(&hmmc, start_addr, end_addr) == HAL_OK)
  #else
  if(HAL_SD_Erase(&hsd, start_addr, end_addr) == HAL_OK)
  #endif
  {
    ret = true;
  }

  return ret;
}

#if HW_SD_EMMC
void SDMMC2_IRQHandler(void)
{
  HAL_MMC_IRQHandler(&hmmc);
}

void HAL_MMC_RxCpltCallback(MMC_HandleTypeDef *hsd)
{
  is_rx_done = true;
}

void HAL_MMC_TxCpltCallback(MMC_HandleTypeDef *hsd)
{
  is_tx_done = true;
}
#else
void SDMMC2_IRQHandler(void)
{
  HAL_SD_IRQHandler(&hsd);
}

void HAL_SD_RxCpltCallback(SD_HandleTypeDef *hsd)
{
  is_rx_done = true;
}

void HAL_SD_TxCpltCallback(SD_HandleTypeDef *hsd)
{
  is_tx_done = true;
}
#endif

void HAL_SD_MspInit(SD_HandleTypeDef* sdHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(sdHandle->Instance==SDMMC2)
  {
    /* SDMMC2 clock enable */
    __HAL_RCC_SDMMC2_CLK_ENABLE();

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**SDMMC2 GPIO Configuration
    PC1     ------> SDMMC2_CK
    PA0     ------> SDMMC2_CMD
    PG9     ------> SDMMC2_D0
    PG10     ------> SDMMC2_D1
    PG12     ------> SDMMC2_D3
    PB3(JTDO/TRACESWO)     ------> SDMMC2_D2
    */
    GPIO_InitStruct.Pin = GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF9_SDMMC2;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_0;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF9_SDMMC2;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_9|GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF11_SDMMC2;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF10_SDMMC2;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF9_SDMMC2;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* SDMMC2 interrupt Init */
    HAL_NVIC_SetPriority(SDMMC2_IRQn, 4, 0);
    HAL_NVIC_EnableIRQ(SDMMC2_IRQn);
  }
}

void HAL_SD_MspDeInit(SD_HandleTypeDef* sdHandle)
{

  if(sdHandle->Instance==SDMMC2)
  {
    /* Peripheral clock disable */
    __HAL_RCC_SDMMC2_CLK_DISABLE();

    /**SDMMC2 GPIO Configuration
    PC1     ------> SDMMC2_CK
    PA0     ------> SDMMC2_CMD
    PG9     ------> SDMMC2_D0
    PG10     ------> SDMMC2_D1
    PG12     ------> SDMMC2_D3
    PB3(JTDO/TRACESWO)     ------> SDMMC2_D2
    */
    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_1);

    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_0);

    HAL_GPIO_DeInit(GPIOG, GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_12);

    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_3);

    /* SDMMC2 interrupt Deinit */
    HAL_NVIC_DisableIRQ(SDMMC2_IRQn);
  }
}

void HAL_MMC_MspInit(MMC_HandleTypeDef* mmcHandle)
{
  HAL_SD_MspInit((SD_HandleTypeDef *)mmcHandle);
}

void HAL_MMC_MspDeInit(MMC_HandleTypeDef* mmcHandle)
{
  HAL_SD_MspDeInit((SD_HandleTypeDef *)mmcHandle);
}


#ifdef _USE_HW_CLI
void cliSd(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info") == true)
  {
    sd_info_t sd_info;

    cliPrintf("sd init      : %d\n", is_init);
    cliPrintf("sd connected : %d\n", is_detected);

    if (is_init == true)
    {
      if (sdGetInfo(&sd_info) == true)
      {
        cliPrintf("   card_type            : %d\n", sd_info.card_type);
        cliPrintf("   card_version         : %d\n", sd_info.card_version);
        cliPrintf("   card_class           : %d\n", sd_info.card_class);
        cliPrintf("   rel_card_Add         : %d\n", sd_info.rel_card_Add);
        cliPrintf("   block_numbers        : %d\n", sd_info.block_numbers);
        cliPrintf("   block_size           : %d\n", sd_info.block_size);
        cliPrintf("   log_block_numbers    : %d\n", sd_info.log_block_numbers);
        cliPrintf("   log_block_size       : %d\n", sd_info.log_block_size);
        cliPrintf("   card_size            : %d MB, %d.%d GB\n", sd_info.card_size, sd_info.card_size/1024, ((sd_info.card_size * 10)/1024) % 10);
      }
    }
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "read") == true)
  {
    uint32_t number;
    uint32_t buf[512/4];

    number = args->getData(1);

    if (sdReadBlocks(number, (uint8_t *)buf, 1, 100) == true)
    {
      for (int i=0; i<512/4; i++)
      {
        cliPrintf("%d:%04d : 0x%08X\n", number, i*4, buf[i]);
      }
    }
    else
    {
      cliPrintf("sdRead Fail\n");
    }

    ret = true;
  }

  if (ret != true)
  {
    cliPrintf("sd info\n");

    if (is_init == true)
    {
      cliPrintf("sd read block_number\n");
    }
  }
}
#endif


#endif
