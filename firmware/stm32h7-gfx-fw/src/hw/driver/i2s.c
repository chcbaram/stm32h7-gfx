/*
 * i2s.c
 *
 *  Created on: 2021. 1. 9.
 *      Author: baram
 */


#include "i2s.h"
#include "cli.h"
#include "gpio.h"
#include "qbuffer.h"
#include "buzzer.h"
#include "files.h"
#include "mixer.h"
#include "nvs.h"


#ifdef _USE_HW_I2S
#include "bsp_math.h"

#define I2S_CFG_NAME "cfg.i2s"

#if HW_I2S_LCD > 0
#include "lcd.h"
#define FFT_LEN         512
#define BLOCK_X_CNT     12
#define BLOCK_Y_CNT     12

typedef struct
{
  uint32_t pre_time_lcd;
  uint8_t  update_cnt;
  q15_t    buf_q15[FFT_LEN*2];

  uint8_t block_target[BLOCK_X_CNT];
  uint8_t block_peak[BLOCK_X_CNT];
  uint8_t block_value[BLOCK_X_CNT];

} i2s_cli_t;

static void drawBlock(int16_t bx, int16_t by, uint16_t color);
static bool lcdUpdate(i2s_cli_t *p_args);
#endif

typedef struct
{
  int16_t volume;
} i2s_cfg_t;

#define I2S_SAMPLERATE_HZ       16000
#define I2S_BUF_MS              (4)
#define I2S_BUF_LEN             (8*1024)
#define I2S_BUF_FRAME_LEN       ((48000 * 2 * I2S_BUF_MS) / 1000)  // 48Khz, Stereo, 4ms


#ifdef _USE_HW_CLI
static void cliI2s(cli_args_t *args);
#endif

static bool is_init = false;
static bool is_started = false;
static bool is_busy = false;
static uint32_t i2s_sample_rate = I2S_SAMPLERATE_HZ;

static int16_t  i2s_frame_buf[I2S_BUF_FRAME_LEN * 2];
static uint32_t i2s_frame_len = 0;
static int16_t  i2s_volume = 100;
static mixer_t   mixer;
static i2s_cfg_t i2s_cfg;

static I2S_HandleTypeDef hi2s1;
static DMA_HandleTypeDef hdma_spi1_tx;






bool i2sInit(void)
{
  bool ret = true;


  hi2s1.Instance = SPI1;
  hi2s1.Init.Mode = I2S_MODE_MASTER_TX;
  hi2s1.Init.Standard = I2S_STANDARD_PHILIPS;
  hi2s1.Init.DataFormat = I2S_DATAFORMAT_16B;
  hi2s1.Init.MCLKOutput = I2S_MCLKOUTPUT_DISABLE;
  hi2s1.Init.AudioFreq = I2S_AUDIOFREQ_16K;
  hi2s1.Init.CPOL = I2S_CPOL_LOW;
  hi2s1.Init.FirstBit = I2S_FIRSTBIT_MSB;
  hi2s1.Init.WSInversion = I2S_WS_INVERSION_DISABLE;
  hi2s1.Init.Data24BitAlignment = I2S_DATA_24BIT_ALIGNMENT_RIGHT;
  hi2s1.Init.MasterKeepIOState = I2S_MASTER_KEEP_IO_STATE_DISABLE;
  if (HAL_I2S_Init(&hi2s1) != HAL_OK)
  {
    ret = false;
  }

  i2s_frame_len = (i2s_sample_rate * 2 * I2S_BUF_MS) / 1000;

  mixerInit(&mixer);
  i2sCfgLoad();

  i2sStart();

  delay(50);
  gpioPinWrite(_PIN_GPIO_SPK_EN, _DEF_HIGH);

  is_init = ret;

  logPrintf("[%s] i2sInit()\n", ret ? "OK" : "NG");

#ifdef _USE_HW_CLI
  cliAdd("i2s", cliI2s);
#endif

  return ret;
}

bool i2sCfgLoad(void)
{
  bool ret = true;

  if (nvsGet(I2S_CFG_NAME, &i2s_cfg, sizeof(i2s_cfg)) == true)
  {
    i2s_volume = i2s_cfg.volume;
  }
  else
  {
    i2s_cfg.volume = i2s_volume;
    ret = nvsSet(I2S_CFG_NAME, &i2s_cfg, sizeof(i2s_cfg));
    logPrintf("[NG] i2sCfgLoad()\n");
  }

  i2sSetVolume(i2s_cfg.volume);
  return ret;
}

bool i2sCfgSave(void)
{
  bool ret = true;

  i2s_cfg.volume = i2s_volume;
  ret = nvsSet(I2S_CFG_NAME, &i2s_cfg, sizeof(i2s_cfg));

  return ret;
}

bool i2sIsBusy(void)
{
  return is_busy;
}

bool i2sSetSampleRate(uint32_t freq)
{
  bool ret = true;
  uint32_t frame_len;
  const uint32_t freq_tbl[7] = 
  {
    I2S_AUDIOFREQ_48K,  
    I2S_AUDIOFREQ_44K,
    I2S_AUDIOFREQ_32K,
    I2S_AUDIOFREQ_22K,
    I2S_AUDIOFREQ_16K,
    I2S_AUDIOFREQ_11K,
    I2S_AUDIOFREQ_8K,
  };

  ret = false;
  for (int i=0; i<7; i++)
  {
    if (freq_tbl[i] == freq)
    {
      ret = true;
      break;
    }
  }
  if (ret != true)
  {
    return false;
  }

  i2sStop();
  delay(10);

  i2s_sample_rate = freq;
  frame_len = (i2s_sample_rate * 2 * I2S_BUF_MS) / 1000;
  i2s_frame_len = frame_len;


  gpioPinWrite(_PIN_GPIO_SPK_EN, _DEF_LOW);

  hi2s1.Init.AudioFreq = freq;
  if (HAL_I2S_Init(&hi2s1) != HAL_OK)
  {
    ret = false;
  }
  i2sStart();

  gpioPinWrite(_PIN_GPIO_SPK_EN, _DEF_HIGH);

  return ret;
}

uint32_t i2sGetSampleRate(void)
{
  return i2s_sample_rate;
}

bool i2sStart(void)
{
  bool ret = false;
  HAL_StatusTypeDef status;
  I2S_HandleTypeDef *p_i2s = &hi2s1;

  memset(i2s_frame_buf, 0, sizeof(i2s_frame_buf));
  status = HAL_I2S_Transmit_DMA(p_i2s, (uint16_t *)i2s_frame_buf, i2s_frame_len * 2);
  if (status == HAL_OK)
  {
    is_started = true;
  }
  else
  {
    is_started = false;
  }

  return ret;
}

bool i2sStop(void)
{
  is_started = false;
  HAL_I2S_DMAStop(&hi2s1);

  return true;
}

int8_t i2sGetEmptyChannel(void)
{
  return mixerGetEmptyChannel(&mixer);
}

uint32_t i2sGetFrameSize(void)
{
  return i2s_frame_len;
}

uint32_t i2sAvailableForWrite(uint8_t ch)
{
  return mixerAvailableForWrite(&mixer, ch);
}

bool i2sWrite(uint8_t ch, int16_t *p_data, uint32_t length)
{
  return mixerWrite(&mixer, ch, p_data, length);
}

uint32_t i2sWriteTimeout(uint8_t ch, int16_t *p_data, uint32_t length, uint32_t timeout)
{
  uint32_t pre_time;
  uint32_t buf_i;
  uint32_t remain;
  uint32_t wr_len;

  buf_i = 0;
  pre_time = millis();
  while(buf_i < length)
  {
    remain = length - buf_i;
    wr_len = cmin(mixerAvailableForWrite(&mixer, ch), remain);

    mixerWrite(&mixer, ch, &p_data[buf_i], wr_len);
    buf_i += wr_len;

    if (millis()-pre_time >= timeout)
    {
      break;
    }
    #ifdef _USE_HW_RTOS
    delay(1);
    #endif
  }

  return buf_i;
}

// https://m.blog.naver.com/PostView.nhn?blogId=hojoon108&logNo=80145019745&proxyReferer=https:%2F%2Fwww.google.com%2F
//
float i2sGetNoteHz(int8_t octave, int8_t note)
{
  float hz;
  float f_note;

  if (octave < 1) octave = 1;
  if (octave > 8) octave = 8;

  if (note <  1) note = 1;
  if (note > 12) note = 12;

  f_note = (float)(note-10)/12.0f;

  hz = pow(2, (octave-1)) * 55 * pow(2, f_note);

  return hz;
}

// https://gamedev.stackexchange.com/questions/4779/is-there-a-faster-sine-function
//
float i2sSin(float x)
{
  const float B = 4 / M_PI;
  const float C = -4 / (M_PI * M_PI);

  return -(B * x + C * x * ((x < 0) ? -x : x));
}

bool i2sPlayBeep(uint32_t freq_hz, uint16_t volume, uint32_t time_ms)
{
  uint32_t pre_time;
  int32_t sample_rate = i2s_sample_rate;
  int32_t num_samples = i2s_frame_len;
  float sample_point;
  int16_t sample[num_samples];
  int16_t sample_index = 0;
  float div_freq;
  int8_t mix_ch;
  int32_t volume_out;


  volume = constrain(volume, 0, 100);
  volume_out = (INT16_MAX/40) * volume / 100;

  mix_ch =  i2sGetEmptyChannel();

  div_freq = (float)sample_rate/(float)freq_hz;

  pre_time = millis();
  while(millis()-pre_time <= time_ms)
  {
    if (i2sAvailableForWrite(mix_ch) >= num_samples)
    {
      for (int i=0; i<num_samples; i+=2)
      {
        sample_point = i2sSin(2.0f * M_PI * (float)(sample_index) / ((float)div_freq));
        sample[i + 0] = (int16_t)(sample_point * volume_out);
        sample[i + 1] = (int16_t)(sample_point * volume_out);
        sample_index = (sample_index + 1) % (int)(div_freq);
      }
      i2sWrite(mix_ch, (int16_t *)sample, num_samples);
    }
    delay(2);
  }

  return true;
}

#if HW_I2S_LCD > 0
bool i2sPlayBeepLcd(i2s_cli_t *p_i2s_cli, uint32_t freq_hz, uint16_t volume, uint32_t time_ms)
{
  uint32_t pre_time;
  int32_t sample_rate = i2s_sample_rate;
  int32_t num_samples = i2s_frame_len;
  float sample_point;
  int16_t sample[num_samples];
  int16_t sample_index = 0;
  float div_freq;
  int8_t mix_ch;
  int32_t volume_out;
  uint32_t q15_buf_index = 0;


  volume = constrain(volume, 0, 100);
  volume_out = (INT16_MAX/40) * volume / 100;

  mix_ch =  i2sGetEmptyChannel();

  div_freq = (float)sample_rate/(float)freq_hz;

  pre_time = millis();
  while(millis()-pre_time <= time_ms)
  {
    if (i2sAvailableForWrite(mix_ch) >= num_samples)
    {
      for (int i=0; i<num_samples; i+=2)
      {
        sample_point = i2sSin(2.0f * M_PI * (float)(sample_index) / ((float)div_freq));
        sample[i + 0] = (int16_t)(sample_point * volume_out);
        sample[i + 1] = (int16_t)(sample_point * volume_out);
        sample_index = (sample_index + 1) % (int)(div_freq);

        if (q15_buf_index < 512)
        {
          p_i2s_cli->buf_q15[q15_buf_index*2 + 0] = sample[i + 0];
          p_i2s_cli->buf_q15[q15_buf_index*2 + 1] = 0;
          
          q15_buf_index++;            
        }
      }
      i2sWrite(mix_ch, (int16_t *)sample, num_samples);
    }
    delay(1);

    if (q15_buf_index == 512)
    {        
      if (lcdUpdate(p_i2s_cli) == true)
      {
        q15_buf_index = 0;
      }        
    }
  }

  lcdLogoOn();

  return true;
}
#endif

int16_t i2sGetVolume(void)
{
  return i2s_volume;
}

bool i2sSetVolume(int16_t volume)
{
  volume = constrain(volume, 0, 100);
  i2s_volume = volume;

  mixerSetVolume(&mixer, i2s_volume);
  return true;
}

void i2sUpdateBuffer(uint8_t index)
{
  if (mixerAvailable(&mixer) >= i2s_frame_len)
  {    
    mixerRead(&mixer, &i2s_frame_buf[index * i2s_frame_len], i2s_frame_len);
    is_busy = true;
  }
  else
  {
    memset(&i2s_frame_buf[index * i2s_frame_len], 0, i2s_frame_len * 2);
    is_busy = false;
  }
}

void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
  i2sUpdateBuffer(0);
}

void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s)
{
  i2sUpdateBuffer(1);
}

void HAL_I2S_ErrorCallback(I2S_HandleTypeDef *hi2s)
{
  return;
}

void SPI1_IRQHandler(void)
{
  HAL_I2S_IRQHandler(&hi2s1);
}

void DMA1_Stream3_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_spi1_tx);
}

void HAL_I2S_MspInit(I2S_HandleTypeDef* i2sHandle)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  if(i2sHandle->Instance==SPI1)
  {

  /** Initializes the peripherals clock
  */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SPI1;
    PeriphClkInitStruct.Spi123ClockSelection = RCC_SPI123CLKSOURCE_PLL;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    /* I2S1 clock enable */
    __HAL_RCC_SPI1_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    /**I2S1 GPIO Configuration
    PA4     ------> I2S1_WS
    PD7     ------> I2S1_SDO
    PG11     ------> I2S1_CK
    */
    GPIO_InitStruct.Pin = GPIO_PIN_4;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_11;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

    /* I2S1 DMA Init */
    /* SPI1_TX Init */
    hdma_spi1_tx.Instance = DMA1_Stream3;
    hdma_spi1_tx.Init.Request = DMA_REQUEST_SPI1_TX;
    hdma_spi1_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_spi1_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_spi1_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_spi1_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_spi1_tx.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma_spi1_tx.Init.Mode = DMA_CIRCULAR;
    hdma_spi1_tx.Init.Priority = DMA_PRIORITY_LOW;
    hdma_spi1_tx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_spi1_tx) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(i2sHandle,hdmatx,hdma_spi1_tx);

    /* I2S1 interrupt Init */
    HAL_NVIC_SetPriority(SPI1_IRQn, 4, 0);
    HAL_NVIC_EnableIRQ(SPI1_IRQn);

    /* DMA1_Stream3_IRQn interrupt configuration */
    HAL_NVIC_SetPriority(DMA1_Stream3_IRQn, 4, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream3_IRQn);    
  }
}

void HAL_I2S_MspDeInit(I2S_HandleTypeDef* i2sHandle)
{

  if(i2sHandle->Instance==SPI1)
  {
    /* Peripheral clock disable */
    __HAL_RCC_SPI1_CLK_DISABLE();

    /**I2S1 GPIO Configuration
    PA4     ------> I2S1_WS
    PD7     ------> I2S1_SDO
    PG11     ------> I2S1_CK
    */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_4);

    HAL_GPIO_DeInit(GPIOD, GPIO_PIN_7);

    HAL_GPIO_DeInit(GPIOG, GPIO_PIN_11);

    /* I2S1 DMA DeInit */
    HAL_DMA_DeInit(i2sHandle->hdmatx);

    /* I2S1 interrupt Deinit */
    HAL_NVIC_DisableIRQ(SPI1_IRQn);
  }
}



#ifdef _USE_HW_CLI


typedef struct wavfile_header_s
{
  char    ChunkID[4];     /*  4   */
  int32_t ChunkSize;      /*  4   */
  char    Format[4];      /*  4   */

  char    Subchunk1ID[4]; /*  4   */
  int32_t Subchunk1Size;  /*  4   */
  int16_t AudioFormat;    /*  2   */
  int16_t NumChannels;    /*  2   */
  int32_t SampleRate;     /*  4   */
  int32_t ByteRate;       /*  4   */
  int16_t BlockAlign;     /*  2   */
  int16_t BitsPerSample;  /*  2   */

  char    Subchunk2ID[4];
  int32_t Subchunk2Size;
} wavfile_header_t;

#if HW_I2S_LCD > 0
static void drawBlock(int16_t bx, int16_t by, uint16_t color)
{
  int16_t x;
  int16_t y;
  int16_t bw;
  int16_t bh;
  int16_t top_space = 32;
  int16_t sw;
  int16_t sh;

  sw = lcdGetWidth();
  sh = lcdGetHeight()-top_space;

  bw = (sw / BLOCK_X_CNT);
  bh = (sh / BLOCK_Y_CNT);

  x = bx*bw;
  y = sh - bh*by - bh;

  lcdDrawFillRect(x, y+top_space, bw-2, bh-2, color);
}

bool lcdUpdate(i2s_cli_t *p_args)
{
  bool ret = false;

  if (millis()-p_args->pre_time_lcd >= 50 && lcdDrawAvailable() == true)
  {
    p_args->pre_time_lcd = millis();

    lcdClearBuffer(black);

    arm_cfft_q15(&arm_cfft_sR_q15_len512, p_args->buf_q15, 0, 1);

    int16_t xi;

    xi = 0;
    for (int i=0; i<BLOCK_X_CNT; i++)
    {
      int32_t h;
      int32_t max_h;


      max_h = 0;
      for (int j=0; j<FFT_LEN/2/BLOCK_X_CNT; j++)
      {
        h = p_args->buf_q15[2*xi + 1];
        h = constrain(h, 0, 500);
        h = cmap(h, 0, 500, 0, 80);
        if (h > max_h)
        {
          max_h = h;
        }
        xi++;
      }
      h = cmap(max_h, 0, 80, 0, BLOCK_Y_CNT-1);

      p_args->block_target[i] = h;

      if (p_args->update_cnt%2 == 0)
      {
        if (p_args->block_peak[i] > 0)
        {
          p_args->block_peak[i]--;
        }
      }
      if (h >= p_args->block_peak[i])
      {
        p_args->block_peak[i] = h;
        p_args->block_value[i] = h;
      }
    }

    p_args->update_cnt++;

    for (int i=0; i<BLOCK_X_CNT; i++)
    {
      drawBlock(i, p_args->block_peak[i], red);

      if (p_args->block_value[i] > p_args->block_target[i])
      {
        p_args->block_value[i]--;
      }
      for (int j=0; j<p_args->block_value[i]; j++)
      {
        drawBlock(i, j, yellow);
      }
    }

    lcdRequestDraw();
    ret = true;
  }

  return ret;
}
#endif


void cliI2s(cli_args_t *args)
{
  bool ret = false;

  #if HW_I2S_LCD > 0
  uint32_t q15_buf_index = 0;
  i2s_cli_t i2s_args;

  memset(i2s_args.block_peak, 0, sizeof(i2s_args.block_peak));
  memset(i2s_args.block_value, 0, sizeof(i2s_args.block_value));
  memset(i2s_args.block_target, 0, sizeof(i2s_args.block_target));
  #endif


  if (args->argc == 1 && args->isStr(0, "info") == true)
  {

    cliPrintf("i2s init      : %d\n", is_init);
    cliPrintf("i2s rate      : %d Khz\n", i2s_sample_rate/1000);
    cliPrintf("i2s buf ms    : %d ms\n", I2S_BUF_MS);
    cliPrintf("i2s frame len : %d \n", i2s_frame_len);
    cliPrintf("i2s volume    : %d \n", i2sGetVolume());
    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "beep") == true)
  {
    uint32_t freq;
    uint32_t time_ms;

    freq = args->getData(1);
    time_ms = args->getData(2);
    
    #if HW_I2S_LCD > 0
    i2sPlayBeepLcd(&i2s_args, freq, 100, time_ms);
    #else
    i2sPlayBeep(freq, 100, time_ms);
    #endif

    ret = true;
  }

  if (args->argc >= 1 && args->isStr(0, "volume") == true)
  {
    uint8_t volume;


    if (args->argc == 1)
    {
      cliPrintf("Volume : %d%%\n", i2sGetVolume());
    }
    else
    {
      volume = args->getData(1);
      i2sSetVolume(volume);
      i2sCfgSave();
      cliPrintf("i2s volume : %d \n", i2sGetVolume());
    }

    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "melody"))
  {
    uint16_t melody[] = {NOTE_C4, NOTE_G3, NOTE_G3, NOTE_A3, NOTE_G3, 0, NOTE_B3, NOTE_C4};
    int note_durations[] = { 4, 8, 8, 4, 4, 4, 4, 4 };

    for (int i=0; i<8; i++) 
    {
      int note_duration = 1000 / note_durations[i];

      i2sPlayBeep(melody[i], 100, note_duration);
      delay(note_duration * 0.3);    
    }
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "play-wav") == true)
  {
    char *file_name;
    FILE *fp;
    wavfile_header_t header;
    uint32_t r_len;
    int32_t  volume = 100;
    int8_t ch;

    file_name = args->getStr(1);

    cliPrintf("FileName      : %s\n", file_name);


    fp = fopen(file_name, "r");
    if (fp == NULL)
    {
      cliPrintf("fopen fail : %s\n", file_name);
      return;
    }
    fread(&header, sizeof(wavfile_header_t), 1, fp);

    cliPrintf("ChunkSize     : %d\n", header.ChunkSize);
    cliPrintf("Format        : %c%c%c%c\n", header.Format[0], header.Format[1], header.Format[2], header.Format[3]);
    cliPrintf("Subchunk1Size : %d\n", header.Subchunk1Size);
    cliPrintf("AudioFormat   : %d\n", header.AudioFormat);
    cliPrintf("NumChannels   : %d\n", header.NumChannels);
    cliPrintf("SampleRate    : %d\n", header.SampleRate);
    cliPrintf("ByteRate      : %d\n", header.ByteRate);
    cliPrintf("BlockAlign    : %d\n", header.BlockAlign);
    cliPrintf("BitsPerSample : %d\n", header.BitsPerSample);
    cliPrintf("Subchunk2Size : %d\n", header.Subchunk2Size);


    i2sSetSampleRate(header.SampleRate);

    r_len = i2sGetFrameSize()/2;

    int16_t buf_frame[i2sGetFrameSize()];

    fseek(fp, sizeof(wavfile_header_t) + 1024, SEEK_SET);

    ch = i2sGetEmptyChannel();

    while(cliKeepLoop())
    {
      int len;


      if (i2sAvailableForWrite(ch) >= i2s_frame_len)
      {
        len = fread(buf_frame, r_len, 2*header.NumChannels, fp);

        if (len != r_len*2*header.NumChannels)
        {
          break;
        }

        int16_t buf_data[2];

        for (int i=0; i<r_len; i++)
        {
          if (header.NumChannels == 2)
          {
            buf_data[0] = buf_frame[i*2 + 0] * volume / 100;;
            buf_data[1] = buf_frame[i*2 + 1] * volume / 100;;
          }
          else
          {
            buf_data[0] = buf_frame[i] * volume / 100;;
            buf_data[1] = buf_frame[i] * volume / 100;;
          }

          #if HW_I2S_LCD > 0
          if (q15_buf_index < FFT_LEN)
          {
            i2s_args.buf_q15[q15_buf_index*2 + 0] = buf_data[0];
            i2s_args.buf_q15[q15_buf_index*2 + 1] = 0;
            
            q15_buf_index++;            
          }
          #endif

          i2sWrite(ch, (int16_t *)buf_data, 2);
        }

        #if HW_I2S_LCD > 0
        if (q15_buf_index == FFT_LEN)
        {        
          if (lcdUpdate(&i2s_args) == true)
          {
            q15_buf_index = 0;
          }        
        }
        #endif
      }
    }
    fclose(fp);

    #if HW_I2S_LCD > 0
    lcdLogoOn();
    #endif

    ret = true;
  }

  if (ret != true)
  {
    cliPrintf("i2s info\n");
    cliPrintf("i2s melody\n");
    cliPrintf("i2s volume 0~100\n");
    cliPrintf("i2s beep freq time_ms\n");
    cliPrintf("i2s play-wav filename\n");
  }
}
#endif

#endif
