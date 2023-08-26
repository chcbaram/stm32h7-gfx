#include "thread.h"


#if _USE_AP_PICOVOICE == 1
#include "picovoice.h"
#include "pv_picovoice.h"
#include "pv_params.h"


namespace picovoice
{
bool init(void);
bool initLib(void);
void callbackWakeWord(void);
void callbackInference(pv_inference_t *inference);
}

static void cliCmd(cli_args_t *args);


__attribute__((section(".thread"))) 
static volatile thread_t thread_obj = 
  {
    .name = "info",
    .init = picovoice::init,
    .main = NULL,
    .priority = osPriorityNormal,
    .stack_size = 1*1024
  };


#define MEMORY_BUFFER_SIZE (70 * 1024)

//-- AccessKey string obtained from Picovoice Console (https://picovoice.ai/console/)
//
static const char *ACCESS_KEY = ""; 

static int8_t memory_buffer[MEMORY_BUFFER_SIZE] __attribute__((aligned(16)));

static const float PORCUPINE_SENSITIVITY        = 0.85f;
static const float RHINO_SENSITIVITY            = 0.5f;
static const float RHINO_ENDPOINT_DURATION_SEC  = 1.0f;
static const bool  RHINO_REQUIRE_ENDPOINT       = true;

static int16_t  pcm_buffer[512];
static uint32_t pcm_index = 0;

static bool is_init = false;
static bool is_wake = false;
static bool is_light_enable = false;

static pv_picovoice_t *handle = NULL;





bool picovoice::init(void)
{
  cliAdd("picovoice", cliCmd);
  return true;
}

bool picovoice::initLib(void)
{
  pv_status_t status;
  

  logPrintf("PICOVOICE VER : %s\n", pv_picovoice_version());


  status = pv_picovoice_init(
          ACCESS_KEY,
          MEMORY_BUFFER_SIZE,
          memory_buffer,
          sizeof(KEYWORD_ARRAY),
          KEYWORD_ARRAY,
          PORCUPINE_SENSITIVITY,
          picovoice::callbackWakeWord,
          sizeof(CONTEXT_ARRAY),
          CONTEXT_ARRAY,
          RHINO_SENSITIVITY,
          RHINO_ENDPOINT_DURATION_SEC,
          RHINO_REQUIRE_ENDPOINT,
          callbackInference,
          &handle);
  if (status != PV_STATUS_SUCCESS) 
  {
    logPrintf("Picovoice init failed with '%s'", pv_status_to_string(status));
    return false;
  }

  const char *rhino_context = NULL;
  status = pv_picovoice_context_info(handle, &rhino_context);
  if (status != PV_STATUS_SUCCESS) 
  {
    logPrintf("retrieving context info failed with '%s'", pv_status_to_string(status));
    logPrintf("pv_picovoice_context_info() error\n");
    return false;
  }
  logPrintf("Rhino context info: ");
  uartWrite(HW_UART_CH_SWD, (uint8_t *)rhino_context, strlen(rhino_context));
  logPrintf("\r\n");

  logPrintf("frame len %d\n", pv_picovoice_frame_length());

  is_init = true;

  return true;
}

void picovoice::callbackWakeWord(void) 
{
  logPrintf("[wake word]\n");
  is_wake = true;
}

void picovoice::callbackInference(pv_inference_t *inference) 
{
  logPrintf("{\n");
  logPrintf("    is_understood : '%s',\n", (inference->is_understood ? "true" : "false"));
  if (inference->is_understood) 
  {
    logPrintf("    intent : '%s',\n", inference->intent);
    if (inference->num_slots > 0) 
    {
      logPrintf("    slots : {\n");
      for (int32_t i = 0; i < inference->num_slots; i++) 
      {
        logPrintf("        '%s' : '%s',\n", inference->slots[i], inference->values[i]);

        if (strcmp(inference->intent, "changeLightState") == 0)
        {
          if (strcmp(inference->slots[i], "state") == 0)
          {
            if (strcmp(inference->values[i], "켜") == 0)
            {
              is_light_enable = true;
            }
            else
            {
              is_light_enable = false;
            }
          }
        }
      }
      logPrintf("    }\n");
    }
  }
  logPrintf("}\n\n");
  pv_inference_delete(inference);

  is_wake = false;
}

void cliCmd(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "test"))
  {
    if (picovoice::initLib() == true)
    {


      pdmBegin();

      while(cliKeepLoop())
      {
        int32_t pdm_len;
        pcm_data_t pcm_data;
        pdm_len = pdmAvailable();
        for (int i=0; i<pdm_len; i++)
        {
          pdmRead(&pcm_data, 1);
          pcm_buffer[pcm_index] = pcm_data.R;
          pcm_index++;
          if (pcm_index == 512)
          {
            pcm_index = 0;

            const pv_status_t status = pv_picovoice_process(handle, pcm_buffer);
            if (status != PV_STATUS_SUCCESS) 
            {
              logPrintf("Picovoice process failed with '%s'", pv_status_to_string(status));
            }        
            break;
          }
        }

        if (lcdDrawAvailable())
        {
          lcdClearBuffer(black);

          if (is_wake == true)
          {
            lcdDrawFillRect(240/2, 64, 240, 64, white);
            lcdPrintfRect(0, 64, LCD_WIDTH, 64, black, 32, 
                          LCD_ALIGN_H_CENTER|LCD_ALIGN_V_CENTER, 
                          "말하세요!!");  
          }

          lcdDrawFillRect((480-150)/2, 240, 150, 150, white);

          if (is_light_enable == true)
            lcdDrawFillRect((480-150+10)/2, 240+5, 140, 140, red);
          else
            lcdDrawFillRect((480-150+10)/2, 240+5, 140, 140, black);

          lcdRequestDraw();
        }

        delay(1);
      }
      pdmEnd();
      lcdLogoOn();
    }
    ret = true;
  }


  if (ret == false)
  {
    cliPrintf("picovoice test\n");
  }
}


#endif