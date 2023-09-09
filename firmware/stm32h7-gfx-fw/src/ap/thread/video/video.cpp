#include "thread.h"


#if _USE_AP_VIDEO == 1
#include "video/lib/AVI_parser.h"




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


static bool is_enable = true;


#define MJPEG_VID_BUFFER_SIZE ((uint32_t)(1024 * 64))
#define MJPEG_AUD_BUFFER_SIZE ((uint32_t)(1024 * 16))

FIL mjpeg_file;          /* MJPEG File object */
AVI_CONTEXT avi_handle;  /* AVI Parser Handle*/

__attribute__ ((aligned (32))) uint8_t mjpeg_video_buffer[MJPEG_VID_BUFFER_SIZE] ;
__attribute__ ((aligned (32))) uint8_t mjpeg_audio_buffer[MJPEG_AUD_BUFFER_SIZE] ;





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

          // cliPrintf("%d/%d %d %d\n", 
          //   avi_handle.CurrentImage, 
          //   avi_handle.aviInfo.TotalFrame,
          //   frame_rate,
          //   avi_handle.FrameSize);

          frame_rate = (millis() - pre_time) + 1;
          if(frame_rate < ((avi_handle.aviInfo.SecPerFrame/1000) * avi_handle.CurrentImage))
          {
            delay(((avi_handle.aviInfo.SecPerFrame /1000) * avi_handle.CurrentImage) - frame_rate);
          }      

        }
        else if (frame_type == AVI_AUDIO_FRAME)
        {
          cliPrintf("AUDIO %d\n", avi_handle.FrameSize);

          // uint32_t buf_i = 0;
          // uint32_t remain;
          // uint32_t wr_len;

          // while(buf_i < avi_handle.FrameSize)
          // {
          //   remain = (avi_handle.FrameSize - buf_i)/2;
          //   wr_len = cmin(i2sAvailableForWrite(i2s_ch), remain);
            
          //   i2sWrite(i2s_ch, (int16_t *)&mjpeg_audio_buffer[buf_i], wr_len);

          //   buf_i += (wr_len * 2);
          // }
          memcpy(&p_buf[buf_index], mjpeg_audio_buffer, avi_handle.FrameSize);
          buf_index += avi_handle.FrameSize;

          delay(1);
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

      uint32_t buf_i = 0;
      uint32_t remain;
      uint32_t wr_len;

      while(buf_i < buf_index)
      {
        remain = (buf_index - buf_i)/2;
        wr_len = cmin(i2sAvailableForWrite(i2s_ch), remain);
        
        i2sWrite(i2s_ch, (int16_t *)&p_buf[buf_i], wr_len);

        buf_i += (wr_len * 2);
      }

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