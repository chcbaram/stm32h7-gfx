#include "flash.h"
#include "qspi.h"
#include "cli.h"


#define FLASH_MAX_BANK            1
#define FLASH_MAX_SECTOR          8
#define FLASH_PAGE_SIZE           32


typedef struct
{
  int16_t  index;
  uint32_t bank;
  uint32_t addr;
  uint32_t length;
} flash_tbl_t;


const flash_tbl_t flash_tbl_bank1[FLASH_MAX_SECTOR] =
    {
        {0, FLASH_BANK_1, 0x08000000, 128*1024},
        {1, FLASH_BANK_1, 0x08020000, 128*1024},
        {2, FLASH_BANK_1, 0x08040000, 128*1024},
        {3, FLASH_BANK_1, 0x08060000, 128*1024},
        {4, FLASH_BANK_1, 0x08080000, 128*1024},
        {5, FLASH_BANK_1, 0x080A0000, 128*1024},
        {6, FLASH_BANK_1, 0x080C0000, 128*1024},
        {7, FLASH_BANK_1, 0x080E0000, 128*1024},
    };



#ifdef _USE_HW_CLI
static void cliFlash(cli_args_t *args);
#endif


static const flash_tbl_t *flash_bank_tbl[FLASH_MAX_BANK] = 
    {
      flash_tbl_bank1,
    };




bool flashInit(void)
{

  logPrintf("[OK] flashInit()\n");

#ifdef _USE_HW_CLI
  cliAdd("flash", cliFlash);
#endif
  return true;
}

bool flashErase(uint32_t addr, uint32_t length)
{
  bool ret = false;

  int32_t start_sector = -1;
  int32_t end_sector = -1;
  uint32_t banks;
  const flash_tbl_t *flash_tbl;


#ifdef _USE_HW_QSPI
  if (addr >= qspiGetAddr() && addr < (qspiGetAddr() + qspiGetLength()))
  {
    ret = qspiErase(addr - qspiGetAddr(), length);
    return ret;
  }
#endif


  HAL_FLASH_Unlock();

  for (banks = 0; banks < FLASH_MAX_BANK; banks++)
  {
    start_sector = -1;
    end_sector = -1;

    flash_tbl = flash_bank_tbl[banks];

    for (int i=0; i<FLASH_MAX_SECTOR; i++)
    {
      bool update = false;
      uint32_t start_addr;
      uint32_t end_addr;


      start_addr = flash_tbl[i].addr;
      end_addr   = flash_tbl[i].addr + flash_tbl[i].length - 1;

      if (start_addr >= addr && start_addr < (addr+length))
      {
        update = true;
      }
      if (end_addr >= addr && end_addr < (addr+length))
      {
        update = true;
      }

      if (addr >= start_addr && addr <= end_addr)
      {
        update = true;
      }
      if ((addr+length-1) >= start_addr && (addr+length-1) <= end_addr)
      {
        update = true;
      }


      if (update == true)
      {
        if (start_sector < 0)
        {
          start_sector = i;
        }
        end_sector = i;
      }
    }

    if (start_sector >= 0)
    {
      FLASH_EraseInitTypeDef EraseInit;
      uint32_t SectorError;
      HAL_StatusTypeDef status;


      EraseInit.Sector       = start_sector;
      EraseInit.NbSectors    = (end_sector - start_sector) + 1;
      EraseInit.TypeErase    = FLASH_TYPEERASE_SECTORS;
      EraseInit.VoltageRange = FLASH_VOLTAGE_RANGE_4;
      EraseInit.Banks        = flash_tbl[start_sector].bank;

      status = HAL_FLASHEx_Erase(&EraseInit, &SectorError);
      if (status == HAL_OK)
      {
        uint32_t erase_len = 0;

        for (int i=start_sector; i<=end_sector; i++)
        {
          erase_len += flash_tbl[i].length;
        }

        // SCB_InvalidateDCache_by_Addr((void *)flash_tbl[start_sector].addr, erase_len);
        ret = true;
      }
    }
  }

  HAL_FLASH_Lock();

  return ret;
}

bool flashWrite(uint32_t addr, uint8_t *p_data, uint32_t length)
{
  bool ret = true;
  uint32_t index;
  uint32_t write_length;
  uint32_t write_addr;
  uint8_t buf[FLASH_PAGE_SIZE];
  uint32_t offset;
  HAL_StatusTypeDef status;


#ifdef _USE_HW_QSPI
  if (addr >= qspiGetAddr() && addr < (qspiGetAddr() + qspiGetLength()))
  {
    ret = qspiWrite(addr - qspiGetAddr(), p_data, length);
    return ret;
  }
#endif


  HAL_FLASH_Unlock();

  index = 0;
  offset = addr%FLASH_PAGE_SIZE;

  if (offset != 0 || length < FLASH_PAGE_SIZE)
  {
    write_addr = addr - offset;
    memcpy(&buf[0], (void *)write_addr, FLASH_PAGE_SIZE);
    memcpy(&buf[offset], &p_data[0], constrain(FLASH_PAGE_SIZE-offset, 0, length));

    status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, write_addr, (uint32_t)buf);
    if (status != HAL_OK)
    {
      return false;
    }

    if (offset == 0 && length < FLASH_PAGE_SIZE)
    {
      index += length;
    }
    else
    {
      index += (FLASH_PAGE_SIZE - offset);
    }
  }


  while(index < length)
  {
    write_length = constrain(length - index, 0, FLASH_PAGE_SIZE);

    status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, addr + index, (uint32_t)&p_data[index]);
    if (status != HAL_OK)
    {
      ret = false;
      break;
    }

    index += write_length;

    if ((length - index) > 0 && (length - index) < FLASH_PAGE_SIZE)
    {
      offset = length - index;
      write_addr = addr + index;
      memcpy(&buf[0], (void *)write_addr, FLASH_PAGE_SIZE);
      memcpy(&buf[0], &p_data[index], offset);

      status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, write_addr, (uint32_t)buf);
      if (status != HAL_OK)
      {
        return false;
      }
      break;
    }
  }

  HAL_FLASH_Lock();

  return ret;
}

bool flashRead(uint32_t addr, uint8_t *p_data, uint32_t length)
{
  bool ret = true;
  uint8_t *p_byte = (uint8_t *)addr;


#ifdef _USE_HW_QSPI
  if (addr >= qspiGetAddr() && addr < (qspiGetAddr() + qspiGetLength()))
  {
    ret = qspiRead(addr - qspiGetAddr(), p_data, length);
    return ret;
  }
#endif

  for (int i=0; i<length; i++)
  {
    p_data[i] = p_byte[i];
  }

  return ret;
}





#ifdef _USE_HW_CLI
void cliFlash(cli_args_t *args)
{
  bool ret = false;
  uint32_t i;
  uint32_t addr;
  uint32_t length;
  uint32_t pre_time;
  bool flash_ret;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("flash addr  : 0x%X\n", 0x8000000);
    cliPrintf("qspi  addr  : 0x%X\n", 0x90000000);
    
    ret = true;
  }

  if(args->argc == 3 && args->isStr(0, "read"))
  {
    uint8_t data;

    addr   = (uint32_t)args->getData(1);
    length = (uint32_t)args->getData(2);

    for (i=0; i<length; i++)
    {
      flash_ret = flashRead(addr+i, &data, 1);

      if (flash_ret == true)
      {
        cliPrintf( "addr : 0x%X\t 0x%02X\n", addr+i, data);
      }
      else
      {
        cliPrintf( "addr : 0x%X\t Fail\n", addr+i);
      }
    }

    ret = true;
  }
    
  if(args->argc == 3 && args->isStr(0, "erase"))
  {
    addr   = (uint32_t)args->getData(1);
    length = (uint32_t)args->getData(2);

    pre_time = millis();
    flash_ret = flashErase(addr, length);

    cliPrintf( "addr : 0x%X\t len : %d %d ms\n", addr, length, (millis()-pre_time));
    if (flash_ret)
    {
      cliPrintf("OK\n");
    }
    else
    {
      cliPrintf("FAIL\n");
    }

    ret = true;
  }
    
  if(args->argc == 3 && args->isStr(0, "write"))
  {
    uint32_t data;

    addr = (uint32_t)args->getData(1);
    data = (uint32_t)args->getData(2);

    pre_time = millis();
    flash_ret = flashWrite(addr, (uint8_t *)&data, 4);

    cliPrintf( "addr : 0x%X\t 0x%X %dms\n", addr, data, millis()-pre_time);
    if (flash_ret)
    {
      cliPrintf("OK\n");
    }
    else
    {
      cliPrintf("FAIL\n");
    }

    ret = true;
  }

  if(args->argc == 3 && args->isStr(0, "check"))
  {
    uint32_t data = 0;


    addr   = (uint32_t)args->getData(1);
    length = (uint32_t)args->getData(2);

    do
    {
      cliPrintf("flashErase()..");
      if (flashErase(addr, length*4) == false)
      {
        cliPrintf("Fail\n");
        break;
      }
      cliPrintf("OK\n");

      cliPrintf("flashWrite()..");
      for (uint32_t i=0; i<length; i++)
      {
        data = i;
        if (flashWrite(addr + i*4, (uint8_t *)&data, 4) == false)
        {
          cliPrintf("Fail %d\n", i);
          break;
        }
      }
      cliPrintf("OK\n");

      cliPrintf("flashRead() ..");
      for (uint32_t i=0; i<1024*1024/4; i++)
      {
        data = 0;
        if (flashRead(qspiGetAddr() + i*4, (uint8_t *)&data, 4) == false)
        {
          cliPrintf("Fail %d\n", i);
          break;
        }
        if (data != i)
        {
          cliPrintf("Check Fail %d\n", i);
          break;
        }
      }  
      cliPrintf("OK\n");


      cliPrintf("flashErase()..");
      if (flashErase(addr, length*4) == false)
      {
        cliPrintf("Fail\n");
        break;
      }
      cliPrintf("OK\n");  
    } while (0);
    
    ret = true;
  }


  if (ret == false)
  {
    cliPrintf( "flash info\n");
    cliPrintf( "flash read  [addr] [length]\n");
    cliPrintf( "flash erase [addr] [length]\n");
    cliPrintf( "flash write [addr] [data]\n");
    cliPrintf( "flash check [addr] [count]\n");
  }
}
#endif