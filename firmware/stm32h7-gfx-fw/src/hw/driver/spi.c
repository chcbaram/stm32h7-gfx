/*
 * spi.c
 *
 *  Created on: 2020. 12. 27.
 *      Author: baram
 */


#include "spi.h"
#include "stm32h7xx_ll_spi.h"


#ifdef _USE_HW_SPI

#define SPI_TX_DMA_MAX_LENGTH   0xFFFF




typedef struct
{
  bool is_open;
  bool is_tx_done;
  bool is_rx_done;
  bool is_error;

  void (*func_tx)(void);

  SPI_HandleTypeDef *h_spi;
  DMA_HandleTypeDef *h_dma_tx;
  DMA_HandleTypeDef *h_dma_rx;
} spi_t;



spi_t spi_tbl[SPI_MAX_CH];

SPI_HandleTypeDef hspi2;
SPI_HandleTypeDef hspi5;
DMA_HandleTypeDef hdma_spi2_rx;




bool spiInit(void)
{
  bool ret = true;


  for (int i=0; i<SPI_MAX_CH; i++)
  {
    spi_tbl[i].is_open = false;
    spi_tbl[i].is_tx_done = true;
    spi_tbl[i].is_rx_done = true;
    spi_tbl[i].is_error = false;
    spi_tbl[i].func_tx = NULL;
    spi_tbl[i].h_dma_rx = NULL;
    spi_tbl[i].h_dma_tx = NULL;
  }

  return ret;
}

bool spiBegin(uint8_t ch)
{
  bool ret = false;
  spi_t *p_spi = &spi_tbl[ch];

  switch(ch)
  {
    case _DEF_SPI1:
      p_spi->h_spi = &hspi2;
      p_spi->h_dma_rx = &hdma_spi2_rx;

      p_spi->h_spi->Instance              = SPI2;
      p_spi->h_spi->Init.Mode             = SPI_MODE_MASTER;
      p_spi->h_spi->Init.Direction        = SPI_DIRECTION_2LINES;
      p_spi->h_spi->Init.DataSize         = SPI_DATASIZE_8BIT;
      p_spi->h_spi->Init.CLKPolarity      = SPI_POLARITY_LOW;
      p_spi->h_spi->Init.CLKPhase         = SPI_PHASE_1EDGE;
      p_spi->h_spi->Init.NSS              = SPI_NSS_SOFT;
      p_spi->h_spi->Init.BaudRatePrescaler= SPI_BAUDRATEPRESCALER_2;
      p_spi->h_spi->Init.FirstBit         = SPI_FIRSTBIT_MSB;
      p_spi->h_spi->Init.TIMode           = SPI_TIMODE_DISABLE;
      p_spi->h_spi->Init.CRCCalculation   = SPI_CRCCALCULATION_DISABLE;
      p_spi->h_spi->Init.CRCPolynomial    = 0;

      p_spi->h_spi->Init.NSSPMode                   = SPI_NSS_PULSE_DISABLE;
      p_spi->h_spi->Init.NSSPolarity                = SPI_NSS_POLARITY_LOW;
      p_spi->h_spi->Init.FifoThreshold              = SPI_FIFO_THRESHOLD_01DATA;
      p_spi->h_spi->Init.TxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
      p_spi->h_spi->Init.RxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
      p_spi->h_spi->Init.MasterSSIdleness           = SPI_MASTER_SS_IDLENESS_01CYCLE;
      p_spi->h_spi->Init.MasterInterDataIdleness    = SPI_MASTER_INTERDATA_IDLENESS_01CYCLE;
      p_spi->h_spi->Init.MasterReceiverAutoSusp     = SPI_MASTER_RX_AUTOSUSP_DISABLE;
      p_spi->h_spi->Init.MasterKeepIOState          = SPI_MASTER_KEEP_IO_STATE_DISABLE;
      p_spi->h_spi->Init.IOSwap                     = SPI_IO_SWAP_DISABLE;

      __HAL_RCC_DMA1_CLK_ENABLE();

      HAL_SPI_DeInit(p_spi->h_spi);
      if (HAL_SPI_Init(p_spi->h_spi) == HAL_OK)
      {
        p_spi->is_open = true;
        ret = true;
      }
      break;

    case _DEF_SPI2:
      p_spi->h_spi = &hspi5;

      p_spi->h_spi->Instance              = SPI5;
      p_spi->h_spi->Init.Mode             = SPI_MODE_MASTER;
      p_spi->h_spi->Init.Direction        = SPI_DIRECTION_1LINE;
      p_spi->h_spi->Init.DataSize         = SPI_DATASIZE_8BIT;
      p_spi->h_spi->Init.CLKPolarity      = SPI_POLARITY_LOW;
      p_spi->h_spi->Init.CLKPhase         = SPI_PHASE_1EDGE;
      p_spi->h_spi->Init.NSS              = SPI_NSS_SOFT;
      p_spi->h_spi->Init.BaudRatePrescaler= SPI_BAUDRATEPRESCALER_16;
      p_spi->h_spi->Init.FirstBit         = SPI_FIRSTBIT_MSB;
      p_spi->h_spi->Init.TIMode           = SPI_TIMODE_DISABLE;
      p_spi->h_spi->Init.CRCCalculation   = SPI_CRCCALCULATION_DISABLE;
      p_spi->h_spi->Init.CRCPolynomial    = 0;

      p_spi->h_spi->Init.NSSPMode                   = SPI_NSS_PULSE_DISABLE;
      p_spi->h_spi->Init.NSSPolarity                = SPI_NSS_POLARITY_LOW;
      p_spi->h_spi->Init.FifoThreshold              = SPI_FIFO_THRESHOLD_01DATA;
      p_spi->h_spi->Init.TxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
      p_spi->h_spi->Init.RxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
      p_spi->h_spi->Init.MasterSSIdleness           = SPI_MASTER_SS_IDLENESS_01CYCLE;
      p_spi->h_spi->Init.MasterInterDataIdleness    = SPI_MASTER_INTERDATA_IDLENESS_01CYCLE;
      p_spi->h_spi->Init.MasterReceiverAutoSusp     = SPI_MASTER_RX_AUTOSUSP_DISABLE;
      p_spi->h_spi->Init.MasterKeepIOState          = SPI_MASTER_KEEP_IO_STATE_DISABLE;
      p_spi->h_spi->Init.IOSwap                     = SPI_IO_SWAP_DISABLE;

      HAL_SPI_DeInit(p_spi->h_spi);
      if (HAL_SPI_Init(p_spi->h_spi) == HAL_OK)
      {
        p_spi->is_open = true;
        ret = true;
      }
      break;

  }

  return ret;
}

void spiSetDataMode(uint8_t ch, uint8_t dataMode)
{
  spi_t  *p_spi = &spi_tbl[ch];


  if (p_spi->is_open == false) return;


  switch( dataMode )
  {
    // CPOL=0, CPHA=0
    case SPI_MODE0:
      p_spi->h_spi->Init.CLKPolarity = SPI_POLARITY_LOW;
      p_spi->h_spi->Init.CLKPhase    = SPI_PHASE_1EDGE;
      HAL_SPI_Init(p_spi->h_spi);
      break;

    // CPOL=0, CPHA=1
    case SPI_MODE1:
      p_spi->h_spi->Init.CLKPolarity = SPI_POLARITY_LOW;
      p_spi->h_spi->Init.CLKPhase    = SPI_PHASE_2EDGE;
      HAL_SPI_Init(p_spi->h_spi);
      break;

    // CPOL=1, CPHA=0
    case SPI_MODE2:
      p_spi->h_spi->Init.CLKPolarity = SPI_POLARITY_HIGH;
      p_spi->h_spi->Init.CLKPhase    = SPI_PHASE_1EDGE;
      HAL_SPI_Init(p_spi->h_spi);
      break;

    // CPOL=1, CPHA=1
    case SPI_MODE3:
      p_spi->h_spi->Init.CLKPolarity = SPI_POLARITY_HIGH;
      p_spi->h_spi->Init.CLKPhase    = SPI_PHASE_2EDGE;
      HAL_SPI_Init(p_spi->h_spi);
      break;
  }
}

void spiSetBitWidth(uint8_t ch, uint8_t bit_width)
{
  spi_t  *p_spi = &spi_tbl[ch];

  if (p_spi->is_open == false) return;

  

  switch(bit_width)
  {
    case 9:
      p_spi->h_spi->Init.DataSize = SPI_DATASIZE_9BIT;
      LL_SPI_SetDataWidth(p_spi->h_spi->Instance, LL_SPI_DATAWIDTH_9BIT);
      break;

    case 16:
      p_spi->h_spi->Init.DataSize = SPI_DATASIZE_16BIT;
      LL_SPI_SetDataWidth(p_spi->h_spi->Instance, LL_SPI_DATAWIDTH_16BIT);
      break;

    default:
      p_spi->h_spi->Init.DataSize = SPI_DATASIZE_8BIT;
      LL_SPI_SetDataWidth(p_spi->h_spi->Instance, LL_SPI_DATAWIDTH_8BIT);
      break;
  }
}

uint8_t spiTransfer8(uint8_t ch, uint8_t data)
{
  uint8_t ret;
  spi_t  *p_spi = &spi_tbl[ch];


  if (p_spi->is_open == false) return 0;

  HAL_SPI_TransmitReceive(p_spi->h_spi, &data, &ret, 1, 10);

  return ret;
}

uint16_t spiTransfer16(uint8_t ch, uint16_t data)
{
  uint8_t tBuf[2];
  uint8_t rBuf[2];
  uint16_t ret;
  spi_t  *p_spi = &spi_tbl[ch];


  if (p_spi->is_open == false) return 0;

  if (p_spi->h_spi->Init.DataSize == SPI_DATASIZE_8BIT)
  {
    tBuf[1] = (uint8_t)data;
    tBuf[0] = (uint8_t)(data>>8);
    HAL_SPI_TransmitReceive(p_spi->h_spi, (uint8_t *)&tBuf, (uint8_t *)&rBuf, 2, 10);

    ret = rBuf[0];
    ret <<= 8;
    ret += rBuf[1];
  }
  else
  {
    HAL_SPI_TransmitReceive(p_spi->h_spi, (uint8_t *)&data, (uint8_t *)&ret, 1, 10);
  }

  return ret;
}

bool spiTransfer(uint8_t ch, uint8_t *tx_buf, uint8_t *rx_buf, uint32_t length, uint32_t timeout)
{
  bool ret = true;
  HAL_StatusTypeDef status;
  spi_t  *p_spi = &spi_tbl[ch];

  if (p_spi->is_open == false) return false;

  if (rx_buf == NULL)
  {
    status =  HAL_SPI_Transmit(p_spi->h_spi, tx_buf, length, timeout);
  }
  else if (tx_buf == NULL)
  {
    status =  HAL_SPI_Receive(p_spi->h_spi, rx_buf, length, timeout);
  }
  else
  {
    status =  HAL_SPI_TransmitReceive(p_spi->h_spi, tx_buf, rx_buf, length, timeout);
  }

  if (status != HAL_OK)
  {
    return false;
  }

  return ret;
}

bool spiTransferDMA(uint8_t ch, uint8_t *tx_buf, uint8_t *rx_buf, uint32_t length, uint32_t timeout)
{
  bool ret = false;
  HAL_StatusTypeDef status;
  spi_t  *p_spi = &spi_tbl[ch];
  bool is_dma = false;

  if (p_spi->is_open == false) return false;

  if (rx_buf == NULL)
  {
    status = HAL_SPI_Transmit(p_spi->h_spi, tx_buf, length, timeout);
  }
  else if (tx_buf == NULL)
  {
    p_spi->is_rx_done = false;
    status = HAL_SPI_Receive_DMA(p_spi->h_spi, rx_buf, length);
    is_dma = true;
  }
  else
  {
    status = HAL_SPI_TransmitReceive(p_spi->h_spi, tx_buf, rx_buf, length, timeout);
  }

  if (status == HAL_OK)
  {
    uint32_t pre_time;

    ret = true;
    pre_time = millis();
    if (is_dma == true)
    {
      while(1)
      {
        if(p_spi->is_rx_done == true)
          break;

        if((millis()-pre_time) >= timeout)
        {
          ret = false;
          break;
        }
      }
    }
  }

  return ret;
}

void spiDmaTxStart(uint8_t spi_ch, uint8_t *p_buf, uint32_t length)
{
  spi_t  *p_spi = &spi_tbl[spi_ch];

  if (p_spi->is_open == false) return;

  p_spi->is_tx_done = false;
  HAL_SPI_Transmit_DMA(p_spi->h_spi, p_buf, length);
}

bool spiDmaTxTransfer(uint8_t ch, void *buf, uint32_t length, uint32_t timeout)
{
  bool ret = true;
  uint32_t t_time;


  spiDmaTxStart(ch, (uint8_t *)buf, length);

  t_time = millis();

  if (timeout == 0) return true;

  while(1)
  {
    if(spiDmaTxIsDone(ch))
    {
      break;
    }
    if((millis()-t_time) > timeout)
    {
      ret = false;
      break;
    }
  }

  return ret;
}

bool spiDmaTxIsDone(uint8_t ch)
{
  spi_t  *p_spi = &spi_tbl[ch];

  if (p_spi->is_open == false)     return true;

  return p_spi->is_tx_done;
}

void spiAttachTxInterrupt(uint8_t ch, void (*func)())
{
  spi_t  *p_spi = &spi_tbl[ch];


  if (p_spi->is_open == false)     return;

  p_spi->func_tx = func;
}


void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
  if (hspi->Instance == spi_tbl[_DEF_SPI1].h_spi->Instance)
  {
    spi_tbl[_DEF_SPI1].is_rx_done = true;
  }  
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
  if (hspi->Instance == spi_tbl[_DEF_SPI1].h_spi->Instance)
  {
    spi_tbl[_DEF_SPI1].is_error = true;
  }
}

void SPI2_IRQHandler(void)
{
  HAL_SPI_IRQHandler(&hspi2);
}

void DMA1_Stream5_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_spi2_rx);
}

void HAL_SPI_MspInit(SPI_HandleTypeDef* spiHandle)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  if(spiHandle->Instance==SPI2)
  {
  /** Initializes the peripherals clock
  */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SPI2;
    PeriphClkInitStruct.Spi123ClockSelection = RCC_SPI123CLKSOURCE_PLL;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    /* SPI2 clock enable */
    __HAL_RCC_SPI2_CLK_ENABLE();

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**SPI2 GPIO Configuration
    PC2_C     ------> SPI2_MISO
    PC3_C     ------> SPI2_MOSI
    PB13     ------> SPI2_SCK
    */
    GPIO_InitStruct.Pin = GPIO_PIN_2|GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* SPI2 DMA Init */
    /* SPI2_RX Init */
    hdma_spi2_rx.Instance = DMA1_Stream5;
    hdma_spi2_rx.Init.Request = DMA_REQUEST_SPI2_RX;
    hdma_spi2_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_spi2_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_spi2_rx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_spi2_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_spi2_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_spi2_rx.Init.Mode = DMA_NORMAL;
    hdma_spi2_rx.Init.Priority = DMA_PRIORITY_LOW;
    hdma_spi2_rx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_spi2_rx) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(spiHandle,hdmarx,hdma_spi2_rx);    

    /* SPI2 interrupt Init */
    HAL_NVIC_SetPriority(SPI2_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(SPI2_IRQn);

    /* DMA1_Stream5_IRQn interrupt configuration */
    HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);    
  }  

  if(spiHandle->Instance==SPI5)
  {
  /** Initializes the peripherals clock
  */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SPI5;
    PeriphClkInitStruct.Spi45ClockSelection = RCC_SPI45CLKSOURCE_D2PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    /* SPI5 clock enable */
    __HAL_RCC_SPI5_CLK_ENABLE();

    __HAL_RCC_GPIOF_CLK_ENABLE();
    /**SPI5 GPIO Configuration
    PF7     ------> SPI5_SCK
    PF9     ------> SPI5_MOSI
    */
    GPIO_InitStruct.Pin = GPIO_PIN_7|GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI5;
    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);
  }
}

void HAL_SPI_MspDeInit(SPI_HandleTypeDef* spiHandle)
{

  if(spiHandle->Instance==SPI2)
  {
    /* Peripheral clock disable */    
    __HAL_RCC_SPI2_CLK_DISABLE();

    /**SPI2 GPIO Configuration
    PC2_C     ------> SPI2_MISO
    PC3_C     ------> SPI2_MOSI
    PB13     ------> SPI2_SCK
    */
    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_2|GPIO_PIN_3);

    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_13);
  }
  
  if(spiHandle->Instance==SPI5)
  {
    /* Peripheral clock disable */
    __HAL_RCC_SPI5_CLK_DISABLE();

    /**SPI5 GPIO Configuration
    PF7     ------> SPI5_SCK
    PF9     ------> SPI5_MOSI
    */
    HAL_GPIO_DeInit(GPIOF, GPIO_PIN_7|GPIO_PIN_9);
  }  
}


#endif