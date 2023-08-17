#include "pdm.h"
#include "cli.h"


typedef struct
{
  uint8_t L;
  uint8_t R;
} pdm_data_t;


#ifdef _USE_HW_CLI
static void cliCmd(cli_args_t *args);
#endif

static SAI_HandleTypeDef hsai_BlockA1;
static DMA_HandleTypeDef hdma_sai1_a;

static bool is_init = false;
// __attribute__((aligned(64)))
pdm_data_t pdm_buf[4096];





bool pdmInit(void)
{
  hsai_BlockA1.Instance                 = SAI1_Block_A;
  hsai_BlockA1.Init.Protocol            = SAI_FREE_PROTOCOL;
  hsai_BlockA1.Init.AudioMode           = SAI_MODEMASTER_RX;
  hsai_BlockA1.Init.DataSize            = SAI_DATASIZE_16;
  hsai_BlockA1.Init.FirstBit            = SAI_FIRSTBIT_MSB;
  hsai_BlockA1.Init.ClockStrobing       = SAI_CLOCKSTROBING_FALLINGEDGE;
  hsai_BlockA1.Init.Synchro             = SAI_ASYNCHRONOUS;
  hsai_BlockA1.Init.OutputDrive         = SAI_OUTPUTDRIVE_ENABLE;
  hsai_BlockA1.Init.NoDivider           = SAI_MASTERDIVIDER_DISABLE;
  hsai_BlockA1.Init.FIFOThreshold       = SAI_FIFOTHRESHOLD_EMPTY;
  hsai_BlockA1.Init.AudioFrequency      = SAI_AUDIO_FREQUENCY_16K * 10;
  hsai_BlockA1.Init.MonoStereoMode      = SAI_STEREOMODE;
  hsai_BlockA1.Init.CompandingMode      = SAI_NOCOMPANDING;
  hsai_BlockA1.Init.PdmInit.Activation  = ENABLE;
  hsai_BlockA1.Init.PdmInit.MicPairsNbr = 1;
  hsai_BlockA1.Init.PdmInit.ClockEnable = SAI_PDM_CLOCK2_ENABLE;
  hsai_BlockA1.FrameInit.FrameLength    = 16;
  hsai_BlockA1.FrameInit.ActiveFrameLength = 1;
  hsai_BlockA1.FrameInit.FSDefinition   = SAI_FS_STARTFRAME;
  hsai_BlockA1.FrameInit.FSPolarity     = SAI_FS_ACTIVE_HIGH;
  hsai_BlockA1.FrameInit.FSOffset       = SAI_FS_FIRSTBIT;
  hsai_BlockA1.SlotInit.FirstBitOffset  = 0;
  hsai_BlockA1.SlotInit.SlotSize        = SAI_SLOTSIZE_DATASIZE;
  hsai_BlockA1.SlotInit.SlotNumber      = 1;
  hsai_BlockA1.SlotInit.SlotActive      = 0x0000FFFF;

  HAL_SAI_MspInit(&hsai_BlockA1);
  if (HAL_SAI_Init(&hsai_BlockA1) != HAL_OK)
  {
    Error_Handler();
  }
  HAL_SAI_MspInit(&hsai_BlockA1);

  uint32_t freq_sai;
  uint32_t freq_sck;
  uint32_t freq_fs;
  int32_t  freq_err;

  freq_sai = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SAI1);
  freq_sck = freq_sai / hsai_BlockA1.Init.Mckdiv;
  freq_fs  = freq_sai / (hsai_BlockA1.FrameInit.FrameLength * hsai_BlockA1.Init.Mckdiv);
  freq_err = (freq_fs - 160000) * 10 / 1000;

  logPrintf("[  ] pdmInit()\n");
  logPrintf("     freq sai: %d Hz\n", freq_sai);
  logPrintf("     freq sck: %d Hz\n", freq_sck);
  logPrintf("     freq pdm: %d Hz\n", freq_sck/2);
  logPrintf("     freq fs : %d Hz, %d.%d%%\n", freq_fs, freq_err/10, freq_err%10);

  is_init = true;

#ifdef _USE_HW_CLI
  cliAdd("pdm", cliCmd);
#endif
  return true;
}


void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
  return;
}

volatile uint32_t cnt = 0;
void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *hsai)
{
  uint16_t frame_len;

  // frame_len = (16000 * 8 * 10) / 1000;
  // HAL_SAI_Receive_DMA(hsai, (uint8_t *)&pdm_buf, frame_len);    
  cnt++;
  return;
}

void SAI1_IRQHandler(void)
{
  HAL_SAI_IRQHandler(&hsai_BlockA1);
}

void DMA1_Stream4_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_sai1_a);
}

static uint32_t SAI1_client =0;

void HAL_SAI_MspInit(SAI_HandleTypeDef* saiHandle)
{


  GPIO_InitTypeDef GPIO_InitStruct;
/* SAI1 */
    if(saiHandle->Instance==SAI1_Block_A)
    {
    /* SAI1 clock enable */
    if (SAI1_client == 0)
    {
       __HAL_RCC_SAI1_CLK_ENABLE();

    /* Peripheral interrupt init*/
    HAL_NVIC_SetPriority(SAI1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(SAI1_IRQn);
    }
    SAI1_client ++;

    /**SAI1_A_Block_A GPIO Configuration
    PE5     ------> SAI1_CK2
    PE6     ------> SAI1_D1
    */
    GPIO_InitStruct.Pin = GPIO_PIN_5|GPIO_PIN_6;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF2_SAI1;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

    /* Peripheral DMA init*/

    hdma_sai1_a.Instance = DMA1_Stream4;
    hdma_sai1_a.Init.Request = DMA_REQUEST_SAI1_A;
    hdma_sai1_a.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_sai1_a.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_sai1_a.Init.MemInc = DMA_MINC_ENABLE;
    hdma_sai1_a.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_sai1_a.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma_sai1_a.Init.Mode = DMA_CIRCULAR;
    hdma_sai1_a.Init.Priority = DMA_PRIORITY_LOW;
    hdma_sai1_a.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_sai1_a) != HAL_OK)
    {
      Error_Handler();
    }

    /* Several peripheral DMA handle pointers point to the same DMA handle.
     Be aware that there is only one channel to perform all the requested DMAs. */
    __HAL_LINKDMA(saiHandle,hdmarx,hdma_sai1_a);
    __HAL_LINKDMA(saiHandle,hdmatx,hdma_sai1_a);

    /* DMA1_Stream4_IRQn interrupt configuration */
    HAL_NVIC_SetPriority(DMA1_Stream4_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream4_IRQn);        
    }
}

void HAL_SAI_MspDeInit(SAI_HandleTypeDef* saiHandle)
{

/* SAI1 */
    if(saiHandle->Instance==SAI1_Block_A)
    {
    SAI1_client --;
    if (SAI1_client == 0)
      {
      /* Peripheral clock disable */
       __HAL_RCC_SAI1_CLK_DISABLE();
      HAL_NVIC_DisableIRQ(SAI1_IRQn);
      }

    /**SAI1_A_Block_A GPIO Configuration
    PE5     ------> SAI1_CK2
    PE6     ------> SAI1_D1
    */
    HAL_GPIO_DeInit(GPIOE, GPIO_PIN_5|GPIO_PIN_6);

    HAL_DMA_DeInit(saiHandle->hdmarx);
    HAL_DMA_DeInit(saiHandle->hdmatx);
    }
}

#ifdef _USE_HW_CLI
void cliCmd(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info") == true)
  {
    cliPrintf("is_init : %s\n", is_init ? "True":"False");
    
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "test"))
  {
    HAL_StatusTypeDef status;
    uint16_t frame_len;

    frame_len = (16000 * 10 * 10) / 1000;
    cliPrintf("len %d\n", frame_len);

    status = HAL_SAI_Receive_DMA(&hsai_BlockA1, (uint8_t *)&pdm_buf, frame_len);    
    cnt = 0;
    delay(1000);
    cliPrintf("cnt %d\n", cnt);

    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("pdm info\n");
    cliPrintf("pdm test\n");
  }
}
#endif