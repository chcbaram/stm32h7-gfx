#include "sdram.h"


#ifdef _USE_HW_SDRAM
#include "cli.h"
#include "fmc.h"


#define SDRAM_TIMEOUT                    ((uint32_t)0xFFFF)


#ifdef _USE_HW_CLI
static void cliSdram(cli_args_t *args);
#endif
static bool sdramMemInit(SDRAM_HandleTypeDef *p_sdram);

extern void HAL_FMC_MspInit(void);
extern void HAL_FMC_MspDeInit(void);

static bool is_init = false;
static SDRAM_HandleTypeDef hsdram1;






bool sdramInit(void)
{
  bool ret = true;;

  
  ret = sdramMemInit(&hsdram1);
  
  logPrintf("[%s] sdramInit()\n", ret ? "OK":"NG");
  if (ret == true)
  {
    logPrintf("     addr : 0x%X\n", SDRAM_MEM_ADDR);
    logPrintf("     size : %dMB\n", (int)(SDRAM_MEM_SIZE/1024/1024));
  }

#ifdef _USE_HW_CLI
  cliAdd("sdram", cliSdram);
#endif

  is_init = ret;

  return ret;
}

bool sdramIsInit(void)
{
  return is_init;
}

uint32_t sdramGetAddr(void)
{
  return SDRAM_MEM_ADDR;
}

uint32_t sdramGetLength(void)
{
  return SDRAM_MEM_SIZE;
}

bool sdramMemInit(SDRAM_HandleTypeDef *p_sdram)
{
  FMC_SDRAM_TimingTypeDef SdramTiming = {0};


  // SDRAM-CLK = 188.888Mhz/2 = 94.4444MHz = 10.58ns
  //
  p_sdram->Instance                 = FMC_SDRAM_DEVICE;
  /* p_sdram->Init */
  p_sdram->Init.SDBank              = FMC_SDRAM_BANK1;
  p_sdram->Init.ColumnBitsNumber    = FMC_SDRAM_COLUMN_BITS_NUM_9;
  p_sdram->Init.RowBitsNumber       = FMC_SDRAM_ROW_BITS_NUM_13;
  p_sdram->Init.MemoryDataWidth     = FMC_SDRAM_MEM_BUS_WIDTH_16;
  p_sdram->Init.InternalBankNumber  = FMC_SDRAM_INTERN_BANKS_NUM_4;
  p_sdram->Init.CASLatency          = FMC_SDRAM_CAS_LATENCY_2;
  p_sdram->Init.WriteProtection     = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
  p_sdram->Init.SDClockPeriod       = FMC_SDRAM_CLOCK_PERIOD_2;
  p_sdram->Init.ReadBurst           = FMC_SDRAM_RBURST_ENABLE;
  p_sdram->Init.ReadPipeDelay       = FMC_SDRAM_RPIPE_DELAY_0;
  /* SdramTiming */
  SdramTiming.LoadToActiveDelay     = 2;  // tMRD
  SdramTiming.ExitSelfRefreshDelay  = 7;  // tXSR
  SdramTiming.SelfRefreshTime       = 4;  // tRAS
  SdramTiming.RowCycleDelay         = 6;  // tRC
  SdramTiming.WriteRecoveryTime     = 2;  // tWR
  SdramTiming.RPDelay               = 2;  // tRP
  SdramTiming.RCDDelay              = 2;  // tRCD

  if (HAL_SDRAM_Init(p_sdram, &SdramTiming) != HAL_OK)
  {
    return false;
  }

  static FMC_SDRAM_CommandTypeDef Command;

  // Step 1: Configure a clock configuration enable command 
  //
  Command.CommandMode            = FMC_SDRAM_CMD_CLK_ENABLE;
  Command.CommandTarget          = FMC_SDRAM_CMD_TARGET_BANK1;
  Command.AutoRefreshNumber      = 1;
  Command.ModeRegisterDefinition = 0;
  if(HAL_SDRAM_SendCommand(p_sdram, &Command, SDRAM_TIMEOUT) != HAL_OK) 
  {
    logPrintf("[  ] sdram step 1 fail\n");
    return false;
  }

  // Step 2: Insert 100 us minimum delay */ 
  // Inserted delay is equal to 1 ms due to systick time base unit (ms)
  //
  delay(1);  
    
  // Step 3: Configure a PALL (precharge all) command 
  //
  Command.CommandMode            = FMC_SDRAM_CMD_PALL;
  Command.CommandTarget          = FMC_SDRAM_CMD_TARGET_BANK1;
  Command.AutoRefreshNumber      = 1;
  Command.ModeRegisterDefinition = 0;  
  if(HAL_SDRAM_SendCommand(p_sdram, &Command, SDRAM_TIMEOUT) != HAL_OK) 
  {
    logPrintf("[  ] sdram step 3 fail\n");
    return false;
  }

  // Step 4: Configure a Refresh command 
  //
  Command.CommandMode            = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
  Command.CommandTarget          = FMC_SDRAM_CMD_TARGET_BANK1;
  Command.AutoRefreshNumber      = 8;
  Command.ModeRegisterDefinition = 0; 
  if(HAL_SDRAM_SendCommand(p_sdram, &Command, SDRAM_TIMEOUT) != HAL_OK) 
  {
    logPrintf("[  ] sdram step 4 fail\n");
    return false;
  }

  // Step 5: Program the external memory mode register
  //
  uint32_t mode_reg = 0;

  mode_reg |= (0 << 0); // Burst Length = 1
  mode_reg |= (0 << 3); // Burst Type
                        //   0 : Sequential
                        //   1 : Interleaved
  mode_reg |= (2 << 4); // CAS Latency Mode 
                        //   2 : 
                        //   3 : 
  mode_reg |= (0 << 7); // Operation Mode
  mode_reg |= (1 << 9); // Write Burst Mode
                        //   0 : Programmed Burst Length
                        //   1 : Single Location Access

  Command.CommandMode            = FMC_SDRAM_CMD_LOAD_MODE;
  Command.CommandTarget          = FMC_SDRAM_CMD_TARGET_BANK1;
  Command.AutoRefreshNumber      = 1;
  Command.ModeRegisterDefinition = mode_reg; 
  if(HAL_SDRAM_SendCommand(p_sdram, &Command, SDRAM_TIMEOUT) != HAL_OK) 
  {
    logPrintf("[  ] sdram step 5 fail\n");
    return false;
  }

  // Step 6: Set the refresh rate counter
  //
  // refresh rate = (COUNT + 1) * SDRAM clock freq (94.4444Mhz)
  //                
  // COUNT = (SDRAM refresh period/Number of rows) - 20
  //       = (64ms / 8192) - 20 
  //       = 7.81us * 94.444 - 20 = 717
  uint32_t refresh_count = 717;

  if(HAL_SDRAM_ProgramRefreshRate(p_sdram, refresh_count) != HAL_OK)
  {
    logPrintf("[  ] sdram step 5 fail\n");
    return false;
  }

  return true;
}

void HAL_SDRAM_MspInit(SDRAM_HandleTypeDef* sdramHandle)
{
  fmcInit();
}

void HAL_SDRAM_MspDeInit(SDRAM_HandleTypeDef* sdramHandle)
{
  fmcDeInit();
}

#ifdef _USE_HW_CLI
void cliSdram(cli_args_t *args)
{
  bool ret = false;
  uint8_t number;
  uint32_t i;
  uint32_t pre_time;
  uint32_t exe_time;


  if(args->argc == 1 && args->isStr(0, "info") == true)
  {
    cliPrintf("sdram init : %s\n", is_init ? "True":"False");
    cliPrintf("sdram addr : 0x%X\n", SDRAM_MEM_ADDR);
    cliPrintf("sdram size : %dMB\n", SDRAM_MEM_SIZE/1024/1024);
    ret = true;
  }

  if(args->argc == 2 && args->isStr(0, "test") == true)
  {
    uint32_t *p_data = (uint32_t *)SDRAM_MEM_ADDR;

    number = (uint8_t)args->getData(1);

    while(number > 0)
    {
      pre_time = millis();
      for (i=0; i<SDRAM_MEM_SIZE/4; i++)
      {
        p_data[i] = i;
      }
      exe_time = millis()-pre_time;
      cliPrintf("Write : %d MB/s, %d ms\n", ((SDRAM_MEM_SIZE/1024/1024) * 1000 / exe_time), exe_time);


      volatile uint32_t data_sum = 0;
      pre_time = millis();
      for (i=0; i<SDRAM_MEM_SIZE/4; i++)
      {
        data_sum += p_data[i];
      }
      exe_time = millis()-pre_time;
      cliPrintf("Read  : %d MB/s, %d ms\n", ((SDRAM_MEM_SIZE/1024/1024) * 1000 / exe_time), exe_time);


      for (i=0; i<SDRAM_MEM_SIZE/4; i++)
      {
        if (p_data[i] != i)
        {
          cliPrintf( "%d : 0x%X fail\n", i, p_data[i]);
          break;
        }
      }

      if (i == SDRAM_MEM_SIZE/4)
      {
        cliPrintf( "Count %d\n", number);
        cliPrintf( "Sdram %d MB OK\n\n", SDRAM_MEM_SIZE/1024/1024);
      }

      number--;

      if (cliAvailable() > 0)
      {
        cliPrintf( "Stop test...\n");
        break;
      }
    }

    ret = true;
  }


  if (ret == false)
  {
    cliPrintf("sdram info\n");
    cliPrintf("sdram test 1~100 \n");
  }
}
#endif
#endif