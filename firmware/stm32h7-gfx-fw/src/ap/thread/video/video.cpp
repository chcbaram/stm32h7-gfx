#include "thread.h"


#if _USE_AP_VIDEO == 1
#include "video/lib/AVI_parser.h"
#include "tjpgd.h"



static void cliCmd(cli_args_t *args);
static bool videoInit(void);
static void videoMain(void const *arg);



__attribute__((section(".thread"))) 
static volatile thread_t thread_obj = 
  {
    .name = "video",
    .init = videoInit,
    .main = videoMain,
    .priority = osPriorityNormal,
    .stack_size = 8*1024
  };

typedef struct
{
  uint32_t width;
  uint32_t height;
  uint8_t *p_buf;
  uint32_t size;
  uint32_t buf_i;
} jpeg_buf_t;

static bool is_enable = true;


#define MJPEG_VID_BUFFER_SIZE ((uint32_t)(1024 * 64))
#define MJPEG_AUD_BUFFER_SIZE ((uint32_t)(1024 * 16))

FIL mjpeg_file;          /* MJPEG File object */
AVI_CONTEXT avi_handle;  /* AVI Parser Handle*/

__attribute__ ((aligned (32))) uint8_t mjpeg_video_buffer[MJPEG_VID_BUFFER_SIZE] ;
__attribute__ ((aligned (32))) uint8_t mjpeg_audio_buffer[MJPEG_AUD_BUFFER_SIZE] ;

static uint8_t jpeg_work_buf[8*1024];  





size_t jpegdDataReader(JDEC *decoder, uint8_t *buffer, size_t size)
{
  jpeg_buf_t *jp_buf = (jpeg_buf_t *)decoder->device;


  if (jp_buf->buf_i < jp_buf->size)
  {
    uint32_t len;

    //len = cmin(size, jp_buf->size - jp_buf->buf_i);
    len = size;
    if (buffer) 
    {
      memcpy(buffer, &jp_buf->p_buf[jp_buf->buf_i], len);
    }
    jp_buf->buf_i += len;
    return len;
  }
  else
  {
    return 0;
  }  
}

int jpegdDataWriter(JDEC* decoder, void* bitmap, JRECT* rectangle)
{
  uint16_t *p_src = (uint16_t *)bitmap;
  uint16_t *p_dst = lcdGetFrameBuffer();
  uint16_t index;

  index = 0;
  for (int y=rectangle->top; y<=rectangle->bottom; y++)
  {
    for (int x=rectangle->left; x<=rectangle->right; x++)
    {
      p_dst[(y*4) * LCD_WIDTH + (x*2)] = p_src[index];
      index++;
    }
  }
  return 1;
}

bool videoInit(void)
{
  cliAdd("video", cliCmd);
  return true;
}

void videoMain(void const *arg)
{
  while(1)
  {
    delay(10);
  }
}


void cliCmd(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("is_enable : %s\n", is_enable ? "True":"False");
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "play"))
  {
    bool is_first_frame;
    uint32_t frame_rate;
    uint32_t avi_ret;
    uint32_t pre_time;
    FRESULT fret;
    uint32_t frame_type;
    
    uint8_t *p_buf = (uint8_t *)(HW_MEM_BUF_ADDR + HW_MEM_BUF_SIZE);
    uint32_t buf_index = 0;

    i2sSetVolume(20);
    do 
    {
      fret = f_open(&mjpeg_file, "video1.avi", FA_READ);
      cliPrintf("[%s] f_open()\n", fret == FR_OK ? "OK":"NG");
      if (fret != FR_OK) 
        break;


      is_first_frame = true;
      frame_rate = 0;

      memset(&avi_handle, 0, sizeof(avi_handle));

      /* parse the AVI file Header*/
      avi_ret = AVI_ParserInit(&avi_handle, &mjpeg_file, mjpeg_video_buffer, MJPEG_VID_BUFFER_SIZE, mjpeg_audio_buffer, MJPEG_AUD_BUFFER_SIZE);
      cliPrintf("[%s] AVI_ParserInit()\n", avi_ret == 0 ? "OK":"NG");
      if (avi_ret != 0)        
        break; 

      cliPrintf("avi info\n");
      cliPrintf("   - FileSize    : %d\n", avi_handle.FileSize);
      cliPrintf("   - FrameSize   : %d\n", avi_handle.FrameSize);
      cliPrintf("   - Width       : %d\n", avi_handle.aviInfo.Width);
      cliPrintf("   - Height      : %d\n", avi_handle.aviInfo.Height);

      cliPrintf("   - AudioType   : %d\n", avi_handle.aviInfo.AudioType);
      cliPrintf("   - AudioBufSize: %d\n", avi_handle.aviInfo.AudioBufSize);
      cliPrintf("   - BaudRate    : %d\n", avi_handle.aviInfo.AudioBaudRate);

      cliPrintf("   - Channels    : %d\n", avi_handle.aviInfo.Channels);
      cliPrintf("   - FrameRate   : %d\n", avi_handle.aviInfo.FrameRate);
      cliPrintf("   - SampleRate  : %d\n", avi_handle.aviInfo.SampleRate);
      cliPrintf("   - SecPerFrame : %d\n", avi_handle.aviInfo.SecPerFrame);
      cliPrintf("   - StreamID    : %d\n", avi_handle.aviInfo.StreamID);
      cliPrintf("   - StreamSize  : %d\n", avi_handle.aviInfo.StreamSize);
      cliPrintf("   - TotalFrame  : %d\n", avi_handle.aviInfo.TotalFrame);

      uint8_t i2s_ch; 
      i2s_ch = i2sGetEmptyChannel();
      i2sSetSampleRate(avi_handle.aviInfo.SampleRate);

      pre_time = millis();
      while(cliKeepLoop())
      {
        frame_type = AVI_GetFrame(&avi_handle, &mjpeg_file);
        if (frame_type == AVI_VIDEO_FRAME)
        {
          avi_handle.CurrentImage++;

          if (avi_handle.CurrentImage%2 != 0)
            continue;

          cliPrintf("%d/%d %d %d\n", 
            avi_handle.CurrentImage, 
            avi_handle.aviInfo.TotalFrame,
            frame_rate,
            avi_handle.FrameSize);

          JDEC decoder;
          JRESULT result;
          jpeg_buf_t jpeg_buf;

          jpeg_buf.width  = avi_handle.aviInfo.Width;
          jpeg_buf.height = avi_handle.aviInfo.Height;
          jpeg_buf.size   = avi_handle.FrameSize;
          jpeg_buf.p_buf  = mjpeg_video_buffer;
          jpeg_buf.buf_i  = 0;

          uint32_t pre_time_jp;

          pre_time_jp = millis();
          lcdDrawAvailable();
          lcdClearBuffer(black);
          result = jd_prepare(&decoder, jpegdDataReader, jpeg_work_buf, 8*1024, &jpeg_buf);
          if (JDR_OK == result) 
          {
            if (jd_decomp(&decoder, jpegdDataWriter, 1) == JDR_OK)
            {
              lcdRequestDraw();
              cliPrintf("jp %d ms\n", millis()-pre_time_jp);         
            }
          }

          frame_rate = (millis() - pre_time) + 1;
          if(frame_rate < ((avi_handle.aviInfo.SecPerFrame/1000) * avi_handle.CurrentImage))
          {
            // delay(((avi_handle.aviInfo.SecPerFrame /1000) * avi_handle.CurrentImage) - frame_rate);
          }      

        }
        else if (frame_type == AVI_AUDIO_FRAME)
        {
          // cliPrintf("AUDIO %d\n", avi_handle.FrameSize);

          i2sWriteTimeout(i2s_ch, (int16_t *)mjpeg_audio_buffer, avi_handle.FrameSize/2, 50);

          memcpy(&p_buf[buf_index], mjpeg_audio_buffer, avi_handle.FrameSize);
          buf_index += avi_handle.FrameSize;          
        }        
        else if (frame_type == AVI_END_FILE)
        {
          cliPrintf("AVI_END_FILE\n");
          break;
        }
        else
        {
          cliPrintf("AVI_ERROR\n");
          break;
        }
        delay(1);

        if (avi_handle.CurrentImage >= avi_handle.aviInfo.TotalFrame)
        {
          break;
        }
      }

      // uint32_t buf_i = 0;
      // uint32_t remain;
      // uint32_t wr_len;

      // while(buf_i < buf_index)
      // {

      //   if (i2sAvailableForWrite(i2s_ch) >= i2sGetFrameSize())
      //   {
      //     remain = (buf_index - buf_i)/2;
      //     wr_len = i2sGetFrameSize();
          
      //     i2sWrite(i2s_ch, (int16_t *)&p_buf[buf_i], wr_len);

      //     buf_i += (wr_len * 2);
      //     // delay(1);
      //   }
      // }

      cliPrintf("[OK] f_close()\n");
      f_close(&mjpeg_file);

    } while(0);

    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("video info\n");
    cliPrintf("video play avi\n");
  }
}


#endif