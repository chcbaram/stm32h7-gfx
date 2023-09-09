#include "pdm.h"
#include "cli.h"



#ifdef _USE_HW_PDM
#include "pdm2pcm_glo.h"
#include "acoustic_sl.h"
#include "i2s.h"
#include "qbuffer.h"
#include "mem.h"


typedef struct
{
  uint8_t L;
  uint8_t R;
} pdm_data_t;



#define lock()      xSemaphoreTake(mutex_lock, portMAX_DELAY);
#define unLock()    xSemaphoreGive(mutex_lock);

#define PDM_FILTER_MS           (10)
#define PDM_FILTER_LEN          ((PDM_SAMPLERATE_HZ * PDM_FILTER_MS) / 1000)

#define PDM_SAMPLERATE_HZ       16000
#define PDM_BUF_MS              (PDM_FILTER_MS)
#define PDM_DECIMATION          (96)
#define PDM_GET_MS_TO_LEN(x)    ((PDM_SAMPLERATE_HZ * (PDM_DECIMATION/8) * (x)) / 1000)  
#define PDM_BUF_FRAME_LEN       PDM_GET_MS_TO_LEN(PDM_BUF_MS)  // 16Khz, 2 mics, 10ms
#define PDM_BUF_LEN             (PDM_BUF_FRAME_LEN * 2)

#define PCM_BUF_MS              (PDM_FILTER_MS)
#define PCM_GET_MS_TO_LEN(x)    ((PDM_SAMPLERATE_HZ * (x)) / 1000)  
#define PCM_BUF_FRAME_LEN       (PCM_GET_MS_TO_LEN(PCM_BUF_MS))
#define PCM_BUF_LEN             (PCM_BUF_FRAME_LEN * 2)
#define PCM_BUF_MSG_Q_LEN       (PCM_BUF_FRAME_LEN * 10)



typedef struct
{
  bool is_req;
  bool is_done;
  uint32_t request_len;
  uint32_t current_len;
  pcm_data_t *p_pcm_buf;
} pdm_record_info_t;

typedef struct 
{
  PDM_Filter_Handler_t    handler[PDM_MIC_MAX_CH];
  PDM_Filter_Config_t     config[PDM_MIC_MAX_CH];
} pdm_filter_t;

typedef struct
{
  int32_t angle;
  AcousticSL_Handler_t handler;
  AcousticSL_Config_t  config;
  int32_t result[2];
} pdm_dir_t;


#ifdef _USE_HW_CLI
static void cliCmd(cli_args_t *args);
#endif
static void pdmFilterInit(int16_t mic_gain);
static void pdmThreadMsg(void const *arg);
static bool pdmToPCM(pdm_data_t *p_data, uint32_t pdm_data_len);
static bool pdmStartDMA(void);
static bool pdmDirInit(void);
// static bool pdmStopDMA(void);


static SAI_HandleTypeDef hsai_BlockA1;
static DMA_HandleTypeDef hdma_sai1_a;

static bool is_init = false;

__attribute__((aligned(64))) static pdm_data_t pdm_buf[PDM_BUF_LEN];
__attribute__((aligned(64))) static pcm_data_t pcm_buf[PCM_BUF_LEN];


static bool is_started = false;
static bool is_enable_dir = true;

static pdm_filter_t pdm_filter;
static qbuffer_t pcm_msg_q;
static pcm_data_t pcm_msg_buf[PCM_BUF_MSG_Q_LEN];

pdm_record_info_t pdm_record_info;

static osMessageQId pdm_msg_q;
static SemaphoreHandle_t mutex_lock;

static pdm_dir_t pdm_dir_info;

__attribute__((section(".thread"))) 
static volatile thread_t pdm_msg_thread = 
  {
    .name = "pdm_msg",
    .init = NULL,
    .main = pdmThreadMsg,
    .priority = osPriorityNormal,
    .stack_size = 1*1024
  };






bool pdmInit(void)
{
  bool ret = false;


  osMessageQDef(pdm_msg_q, 8, uint32_t);
  pdm_msg_q = osMessageCreate(osMessageQ(pdm_msg_q), NULL);
  mutex_lock = xSemaphoreCreateMutex();

  qbufferCreateBySize(&pcm_msg_q, (uint8_t *)pcm_msg_buf, sizeof(pcm_data_t), PCM_BUF_MSG_Q_LEN);

  pdm_record_info.is_req = false;
  pdm_record_info.is_done = false;
  

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
  hsai_BlockA1.Init.AudioFrequency      = SAI_AUDIO_FREQUENCY_16K * (PDM_DECIMATION/8);
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
  freq_err = (freq_fs - (PDM_SAMPLERATE_HZ * (PDM_DECIMATION/8))) * 10 / 1000;

  logPrintf("[  ] pdmInit()\n");
  logPrintf("     freq sai: %d Hz\n", freq_sai);
  logPrintf("          sai: %d Hz\n", freq_sai);
  logPrintf("          sck: %d Hz\n", freq_sck);
  logPrintf("          pdm: %d Hz\n", freq_sck/2);
  logPrintf("          fs : %d Hz, %d.%d%%\n", freq_fs, freq_err/10, freq_err%10);
  logPrintf("     PDM_SAMPLERATE_HZ : %d Hz\n", PDM_SAMPLERATE_HZ);
  logPrintf("     PDM_BUF_MS        : %d ms\n", PDM_BUF_MS);
  logPrintf("     PDM_DECIMATION    : %d \n",   PDM_DECIMATION);
  logPrintf("     PDM_BUF_FRAME_LEN : %d \n",   PDM_BUF_FRAME_LEN);
  logPrintf("     pdm buf len : %d KB\n", sizeof(pdm_buf)/1024);
  logPrintf("     pcm buf len : %d KB\n", sizeof(pcm_buf)/1024);

  ret = pdmDirInit();
  logPrintf("[%s] pdmDirInit()\n", ret ? "OK":"NG");
  logPrintf("     mem : %d\n", pdm_dir_info.handler.internal_memory_size);
  is_enable_dir = ret;

  pdmFilterInit(30);
  pdmStartDMA();

  is_init = true;

#ifdef _USE_HW_CLI
  cliAdd("pdm", cliCmd);
#endif
  return true;
}

bool pdmBegin(void)
{
  lock();
  if (is_started == true)
  {
    is_started = false;
    delay(20);
  }

  qbufferFlush(&pcm_msg_q);
  is_started = true;
  unLock();
  return true;
}

bool pdmEnd(void)
{
  is_started = false;
  pdmRecordStop();
  return true;
}

bool pdmIsBusy(void)
{
  return is_started;
}

bool pdmRecordStart(pcm_data_t *p_buf, uint32_t length)
{
  if (is_started != true)
    return false;

  lock();
  pdm_record_info.is_req = true;
  pdm_record_info.is_done = false;
  pdm_record_info.p_pcm_buf = p_buf;
  pdm_record_info.request_len = length;
  pdm_record_info.current_len = 0;
  unLock();
  return true;
}

bool pdmRecordStop(void)
{
  lock();  
  pdm_record_info.is_done = true;
  unLock();
  return true;
}

bool pdmRecordIsBusy(void)
{
  bool ret = false;

  if (pdm_record_info.is_req == true)
  {
    if (pdm_record_info.is_done == false)
    {
      ret = true;
    }
  }

  return ret;
}

bool pdmRecordIsDone(void)
{
  return pdm_record_info.is_done;
}

uint32_t pdmRecordGetLength(void)
{
  return pdm_record_info.current_len;
}

uint32_t pdmAvailable(void)
{
  return qbufferAvailable(&pcm_msg_q);
}

uint32_t pdmGetSampleRate(void)
{
  return PDM_SAMPLERATE_HZ;
}

bool pdmRead(pcm_data_t *p_buf, uint32_t length)
{
  bool ret;

  ret = qbufferRead(&pcm_msg_q, (uint8_t *)p_buf, length);

  return ret;
}

uint32_t pdmGetTimeToLengh(uint32_t ms)
{
  return PCM_GET_MS_TO_LEN(ms);
}

bool pdmDirInit(void)
{
  bool ret = true;

  volatile uint32_t error_value = 0;
  /* Enable CRC peripheral to unlock the library */
  __CRC_CLK_ENABLE();

  /*Setup Source Localization static parameters*/
  pdm_dir_info.handler.channel_number = 2;
  pdm_dir_info.handler.M12_distance   = 300;
  pdm_dir_info.handler.sampling_frequency = PDM_SAMPLERATE_HZ;
  pdm_dir_info.handler.algorithm = ACOUSTIC_SL_ALGORITHM_GCCP;
  pdm_dir_info.handler.ptr_M1_channels = 2;
  pdm_dir_info.handler.ptr_M2_channels = 2;
  pdm_dir_info.handler.ptr_M3_channels = 2;
  pdm_dir_info.handler.ptr_M4_channels = 2;
  pdm_dir_info.handler.samples_to_process = 512;
  (void)AcousticSL_getMemorySize(&pdm_dir_info.handler);
  pdm_dir_info.handler.pInternalMemory = (uint32_t *)malloc(pdm_dir_info.handler.internal_memory_size);
  error_value += AcousticSL_Init(&pdm_dir_info.handler);

  /*Setup Source Localization dynamic parameters*/
  pdm_dir_info.config.resolution = 2;
  pdm_dir_info.config.threshold = 24;
  error_value += AcousticSL_setConfig(&pdm_dir_info.handler, &pdm_dir_info.config);

  /*Error Management*/
  if (error_value != 0U)
  {
    logPrintf("[  ] AcousticSL_setConfig() Err 0x%X\n", error_value);
    ret = false;
  }

  /*Malloc Failure*/
  if (pdm_dir_info.handler.pInternalMemory == NULL)
  {
    ret = false;
  }

  return ret;
}
bool pdmDirEnable(void)
{
  is_enable_dir = true;
  return true;
}

bool pdmDirDisable(void)
{
  is_enable_dir = false;
  return true;
}

int32_t pdmDirGetAngle(void)
{
  return pdm_dir_info.angle;
}

bool pdmDirProcess(void)
{
  bool ret = true;


  for (int i=0; i<PCM_BUF_FRAME_LEN; i+=PCM_GET_MS_TO_LEN(1))
  {
    ret = AcousticSL_Data_Input((int16_t *)&pcm_buf[i].R, (int16_t *)&pcm_buf[i].L,
                              NULL, NULL, &pdm_dir_info.handler);
    if (ret == true)
    {
      (void)AcousticSL_Process((int32_t *)&pdm_dir_info.result, &pdm_dir_info.handler);

      if (pdm_dir_info.result[0] == ACOUSTIC_SL_NO_AUDIO_DETECTED)
      {
        pdm_dir_info.angle = 0;
      }
      else
      {
        pdm_dir_info.angle = pdm_dir_info.result[0];
      }
    }
  }
  return ret;
}

void pdmThreadMsg(void const *arg)
{
  (void)arg;
  osEvent evt;
  bool ret;

  while(1)
  {
    evt = osMessageGet(pdm_msg_q, osWaitForever);
    if (evt.status == osEventMessage)
    {  
      ret = false;
      if (evt.value.v == 0)
      {
        ret = pdmToPCM(&pdm_buf[0], PDM_BUF_FRAME_LEN);
      }
      if (evt.value.v == 1)
      {
        ret = pdmToPCM(&pdm_buf[PDM_BUF_FRAME_LEN], PDM_BUF_FRAME_LEN);
      }        

      if (ret == true)
      {
        if (is_enable_dir)
          pdmDirProcess();
        else
          pdm_dir_info.angle = 0;
      }
    }
  }
}

void pdmFilterInit(int16_t mic_gain)
{

  mic_gain = constrain(mic_gain, -12, 51);

  __HAL_RCC_CRC_CLK_ENABLE();

  for(int i=0; i<PDM_MIC_MAX_CH; i++)
  {
    /* Init PDM filters */
    pdm_filter.handler[i].bit_order  = PDM_FILTER_BIT_ORDER_LSB;
    pdm_filter.handler[i].endianness = PDM_FILTER_ENDIANNESS_LE;
    pdm_filter.handler[i].high_pass_tap = 2122358088;
    pdm_filter.handler[i].out_ptr_channels = 2;
    pdm_filter.handler[i].in_ptr_channels  = 2;
    PDM_Filter_Init((PDM_Filter_Handler_t *)(&pdm_filter.handler[i]));

    /* PDM lib config phase */
    pdm_filter.config[i].output_samples_number = PDM_FILTER_LEN;
    pdm_filter.config[i].mic_gain = mic_gain; 

    #ifdef PDM_FILTER_DEC_FACTOR_64_HI_PERF
      pdm_filter.config[i].bit_depth = PDM_FILTER_BITDEPTH_16;
      if (PDM_DECIMATION == 64)
        pdm_filter.config[i].decimation_factor = PDM_FILTER_DEC_FACTOR_64_HI_PERF;
      else
        pdm_filter.config[i].decimation_factor = PDM_FILTER_DEC_FACTOR_96_HI_PERF;
    #else
      if (PDM_DECIMATION == 64)
        pdm_filter.config[i].decimation_factor = PDM_FILTER_DEC_FACTOR_64;
      else
        pdm_filter.config[i].decimation_factor = PDM_FILTER_DEC_FACTOR_80;
    #endif
    PDM_Filter_setConfig((PDM_Filter_Handler_t *)&pdm_filter.handler[i], &pdm_filter.config[i]);
  }
}

bool pdmFilterProcess(uint8_t *p_pdm_buf, int16_t *p_pcm_buf)
{
  bool ret = true;
  uint32_t pdm_ret = 0;

  for(int i=0; i<PDM_MIC_MAX_CH; i++)
  {
    pdm_ret = PDM_Filter(&((uint8_t*)(p_pdm_buf))[i], (uint16_t*)&(p_pcm_buf[i]), &pdm_filter.handler[i]);
    if (pdm_ret != 0)
    {
      ret = false;
      break;
    }  
  }

  return ret;
}

bool pdmToPCM(pdm_data_t *p_data, uint32_t pdm_data_len)
{
  bool ret = true;

  
  #ifdef _USE_HW_CACHE
  SCB_InvalidateDCache_by_Addr((uint32_t *)p_data, pdm_data_len * 2);
  #endif

  ret = pdmFilterProcess((uint8_t *)p_data, (int16_t *)pcm_buf);
  if (ret == true)
  {
    lock();
    if (is_started)
    {
      // #ifdef _USE_HW_CACHE
      // SCB_CleanDCache_by_Addr((uint32_t*)pcm_buf, PCM_BUF_FRAME_LEN * 4);
      // #endif
      qbufferWrite(&pcm_msg_q, (uint8_t *)pcm_buf, PCM_BUF_FRAME_LEN);

      if (pdm_record_info.is_req == true && pdm_record_info.is_done == false)
      {
        uint32_t record_len;
        pcm_data_t *p_buf;

        record_len = pdm_record_info.request_len - pdm_record_info.current_len;
        if (record_len > PCM_BUF_FRAME_LEN)
        {
          record_len = PCM_BUF_FRAME_LEN;
        }
        p_buf = &pdm_record_info.p_pcm_buf[pdm_record_info.current_len];
        memcpy(p_buf, pcm_buf, PCM_BUF_FRAME_LEN * sizeof(pcm_data_t));

        pdm_record_info.current_len += record_len;
        if (pdm_record_info.current_len >= pdm_record_info.request_len)
        {
          pdm_record_info.is_done = true;
        }
      }
    }
    unLock();
  }
  return ret;
}

bool pdmStartDMA(void)
{
  lock();
  HAL_SAI_Receive_DMA(&hsai_BlockA1, (uint8_t *)&pdm_buf, PDM_BUF_LEN);  
  unLock();
  return true;
}

bool pdmStopDMA(void)
{
  lock();
  HAL_SAI_DMAStop(&hsai_BlockA1);
  unLock();
  return true;
}


void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
  osMessagePut(pdm_msg_q, 0, 10);
}

void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *hsai)
{
  osMessagePut(pdm_msg_q, 1, 10);
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
    cliPrintf("is_init       : %s\n", is_init ? "True":"False");
    cliPrintf("is_enable_dir : %s\n", is_enable_dir ? "True":"False");
    
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "record"))
  {
    uint32_t pre_time;
    uint32_t pre_time_log;
    pcm_data_t *pcm_record_buf;
    uint32_t record_time_ms = 5000;
    uint32_t pcm_record_len = PCM_GET_MS_TO_LEN(record_time_ms);


    pcm_record_buf = memMalloc(pcm_record_len * sizeof(pcm_data_t));

    pdmBegin();
    cliPrintf("record start\n");
    pre_time = millis();
    pre_time_log = millis();

    pdmRecordStart(pcm_record_buf, pcm_record_len);
    while(cliKeepLoop())
    {

      if (millis()-pre_time_log >= 100)
      {
        pre_time_log = millis();
        cliPrintf("progress %d %%\r", pdmRecordGetLength() * 100 / pcm_record_len);
      }

      if (pdmRecordIsDone())
      {
        break;
      }
      delay(1);
    }    
    cliPrintf("\n");
    cliPrintf("pdm end %d ms\n", millis()-pre_time);

    uint8_t i2s_ch = i2sGetEmptyChannel();
    uint32_t play_i = 0;
    uint32_t play_len = PCM_GET_MS_TO_LEN(record_time_ms);

    cliPrintf("play start\n");

    while(cliKeepLoop())
    {
      uint32_t len;
      
      len = i2sAvailableForWrite(i2s_ch);
      len -= (len % 2);

      if (len > 0)
      {
        i2sWrite(i2s_ch, (int16_t *)&pcm_record_buf[play_i], len);
        play_i += (len/2);
      }
      delay(1);

      if (play_i >= play_len)
        break;
    }
    cliPrintf("play end\n");

    pdmEnd();

    memFree(pcm_record_buf);
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "read"))
  {
    pdmBegin();
    while(cliKeepLoop())
    {
      uint32_t pdm_len;

      pdm_len = pdmAvailable();

      if (pdm_len > 0)
      {
        pdmRead(NULL, pdm_len);
        cliPrintf("len %d \n", pdm_len);
      }

      delay(1);
    }
    pdmEnd();
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("pdm info\n");
    cliPrintf("pdm record\n");
    cliPrintf("pdm read\n");
  }
}
#endif

#endif