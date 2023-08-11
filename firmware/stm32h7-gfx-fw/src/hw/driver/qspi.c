#include "qspi.h"
#include "qspi/w25q128fv.h"





/* QSPI Error codes */
#define QSPI_OK            ((uint8_t)0x00)
#define QSPI_ERROR         ((uint8_t)0x01)
#define QSPI_BUSY          ((uint8_t)0x02)
#define QSPI_NOT_SUPPORTED ((uint8_t)0x04)
#define QSPI_SUSPENDED     ((uint8_t)0x08)



/* QSPI Base Address */
#define QSPI_BASE_ADDRESS          0x90000000



/* QSPI Info */
typedef struct {
  uint32_t FlashSize;          /*!< Size of the flash */
  uint32_t EraseSectorSize;    /*!< Size of sectors for the erase operation */
  uint32_t EraseSectorsNumber; /*!< Number of sectors for the erase operation */
  uint32_t ProgPageSize;       /*!< Size of pages for the program operation */
  uint32_t ProgPagesNumber;    /*!< Number of pages for the program operation */

  uint8_t  device_id[20];
} QSPI_Info;


static bool is_init = false;

uint8_t BSP_QSPI_Init(void);
uint8_t BSP_QSPI_DeInit(void);
uint8_t BSP_QSPI_Read(uint8_t* p_data, uint32_t addr, uint32_t length);
uint8_t BSP_QSPI_Write(uint8_t* p_data, uint32_t addr, uint32_t length);
uint8_t BSP_QSPI_Erase_Block(uint32_t block_addr);
uint8_t BSP_QSPI_Erase_Sector(uint32_t SectorAddress);
uint8_t BSP_QSPI_Erase_Chip (void);
uint8_t BSP_QSPI_GetStatus(void);
uint8_t BSP_QSPI_GetInfo(QSPI_Info* p_info);
uint8_t BSP_QSPI_EnableMemoryMappedMode(void);
uint8_t BSP_QSPI_GetID(QSPI_Info* p_info);
uint8_t BSP_QSPI_Config(void);
uint8_t BSP_QSPI_Reset(void);



bool qspiInit(void)
{
  bool ret = true;
  QSPI_Info info;


  if (BSP_QSPI_Init() == QSPI_OK)
  {
    ret = true;
  }
  else
  {
    ret = false;
  }


  if (BSP_QSPI_GetID(&info) == QSPI_OK)
  {
    if (info.device_id[0] == 0xEF && info.device_id[1] == 0x40 && info.device_id[2] == 0x18)
    {
      logPrintf("[OK] qspiInit()\n");
      logPrintf("     W25Q128JV Found\r\n");
      ret = true;
    }
    else
    {
      logPrintf("[OK] qspiInit()\n");
      logPrintf("     W25Q128JV Not Found %X %X %X\r\n", info.device_id[0], info.device_id[1], info.device_id[2]);
      ret = false;
    }
  }
  else
  {
    logPrintf("[NG] qspiInit()\n");
    ret = false;
  }



  is_init = ret;

  return ret;
}

bool qspiReset(void)
{
  bool ret = false;



  if (is_init == true)
  {
    if (BSP_QSPI_Reset() == QSPI_OK)
    {
      ret = true;
    }
  }

  return ret;
}

bool qspiIsInit(void)
{
  return is_init;
}

bool qspiRead(uint32_t addr, uint8_t *p_data, uint32_t length)
{
  uint8_t ret;

  if (addr >= qspiGetLength())
  {
    return false;
  }

  ret = BSP_QSPI_Read(p_data, addr, length);

  if (ret == QSPI_OK)
  {
    return true;
  }
  else
  {
    return false;
  }
}

bool qspiWrite(uint32_t addr, uint8_t *p_data, uint32_t length)
{
  uint8_t ret;

  if (addr >= qspiGetLength())
  {
    return false;
  }

  ret = BSP_QSPI_Write(p_data, addr, length);

  if (ret == QSPI_OK)
  {
    return true;
  }
  else
  {
    return false;
  }
}

bool qspiEraseBlock(uint32_t block_addr)
{
  uint8_t ret;

  ret = BSP_QSPI_Erase_Block(block_addr);

  if (ret == QSPI_OK)
  {
    return true;
  }
  else
  {
    return false;
  }
}

bool qspiEraseSector(uint32_t sector_addr)
{
  uint8_t ret;

  ret = BSP_QSPI_Erase_Sector(sector_addr);

  if (ret == QSPI_OK)
  {
    return true;
  }
  else
  {
    return false;
  }  
}
bool qspiErase(uint32_t addr, uint32_t length)
{
  bool ret = true;
  uint32_t flash_length;
  uint32_t block_size;
  uint32_t block_begin;
  uint32_t block_end;
  uint32_t i;



  flash_length = W25Q128FV_FLASH_SIZE;
  block_size   = W25Q128FV_SECTOR_SIZE;


  if ((addr > flash_length) || ((addr+length) > flash_length))
  {
    return false;
  }
  if (length == 0)
  {
    return false;
  }


  block_begin = addr / block_size;
  block_end   = (addr + length - 1) / block_size;


  for (i=block_begin; i<=block_end; i++)
  {
    ret = qspiEraseSector(block_size*i);
    if (ret == false)
    {
      break;
    }
  }

  return ret;
}

bool qspiEraseChip(void)
{
  uint8_t ret;

  ret = BSP_QSPI_Erase_Chip();

  if (ret == QSPI_OK)
  {
    return true;
  }
  else
  {
    return false;
  }
}

bool qspiGetStatus(void)
{
  uint8_t ret;

  ret = BSP_QSPI_GetStatus();

  if (ret == QSPI_OK)
  {
    return true;
  }
  else
  {
    return false;
  }
}

bool qspiGetInfo(qspi_info_t* p_info)
{
  uint8_t ret;

  ret = BSP_QSPI_GetInfo((QSPI_Info *)p_info);

  if (ret == QSPI_OK)
  {
    return true;
  }
  else
  {
    return false;
  }
}

bool qspiEnableMemoryMappedMode(void)
{
  uint8_t ret;

  ret = BSP_QSPI_EnableMemoryMappedMode();

  if (ret == QSPI_OK)
  {
    return true;
  }
  else
  {
    return false;
  }
}

uint32_t qspiGetAddr(void)
{
  return QSPI_BASE_ADDRESS;
}

uint32_t qspiGetLength(void)
{
  return W25Q128FV_FLASH_SIZE;
}



static OSPI_HandleTypeDef hqspi;



static uint8_t QSPI_ResetMemory(OSPI_HandleTypeDef *hqspi);
static uint8_t QSPI_WriteEnable(OSPI_HandleTypeDef *hqspi);
static uint8_t QSPI_AutoPollingMemReady(OSPI_HandleTypeDef *hqspi, uint32_t Timeout);
static uint8_t QSPI_ReadStatus(OSPI_HandleTypeDef *hqspi, uint8_t cmd, uint8_t *p_data);
static uint8_t QSPI_WriteStatus(OSPI_HandleTypeDef *hqspi, uint8_t cmd, uint8_t data);


uint8_t BSP_QSPI_Init(void)
{
  OSPIM_CfgTypeDef sOspiManagerCfg = {0};


  hqspi.Instance = OCTOSPI1;
  /* Call the DeInit function to reset the driver */
  if (HAL_OSPI_DeInit(&hqspi) != HAL_OK)
  {
    return QSPI_ERROR;
  }


  /* QSPI initialization */
  /* ClockPrescaler set to 1, so QSPI clock = 110MHz / (1) = 110MHz */
  hqspi.Init.FifoThreshold       = 4;
  hqspi.Init.DualQuad            = HAL_OSPI_DUALQUAD_DISABLE;
  hqspi.Init.MemoryType          = HAL_OSPI_MEMTYPE_MICRON;
  hqspi.Init.DeviceSize          = POSITION_VAL(W25Q128FV_FLASH_SIZE);
  hqspi.Init.ChipSelectHighTime  = 5;
  hqspi.Init.FreeRunningClock    = HAL_OSPI_FREERUNCLK_DISABLE;
  hqspi.Init.ClockMode           = HAL_OSPI_CLOCK_MODE_0;
  hqspi.Init.WrapSize            = HAL_OSPI_WRAP_NOT_SUPPORTED;
  hqspi.Init.ClockPrescaler      = 1;
  hqspi.Init.SampleShifting      = HAL_OSPI_SAMPLE_SHIFTING_HALFCYCLE;
  hqspi.Init.DelayHoldQuarterCycle = HAL_OSPI_DHQC_DISABLE;
  hqspi.Init.ChipSelectBoundary  = 0;
  hqspi.Init.DelayBlockBypass    = HAL_OSPI_DELAY_BLOCK_BYPASSED;
  hqspi.Init.MaxTran             = 0;
  hqspi.Init.Refresh             = 0;
  if (HAL_OSPI_Init(&hqspi) != HAL_OK)
  {
    logPrintf("HAL_QSPI_Init() fail\n");
    return QSPI_ERROR;
  }

  sOspiManagerCfg.ClkPort = 1;
  sOspiManagerCfg.NCSPort = 1;
  sOspiManagerCfg.IOLowPort = HAL_OSPIM_IOPORT_1_HIGH;
  if (HAL_OSPIM_Config(&hqspi, &sOspiManagerCfg, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    Error_Handler();
  }


  /* QSPI memory reset */
  if (QSPI_ResetMemory(&hqspi) != QSPI_OK)
  {
    logPrintf("QSPI_ResetMemory() fail\n");
    return QSPI_NOT_SUPPORTED;
  }

  if (BSP_QSPI_Config() != QSPI_OK)
  {
    logPrintf("QSPI_Config() fail\n");
    return QSPI_NOT_SUPPORTED;
  }

  return QSPI_OK;
}

uint8_t BSP_QSPI_Reset(void)
{
  if (QSPI_ResetMemory(&hqspi) != QSPI_OK)
  {
    return QSPI_NOT_SUPPORTED;
  }

  return QSPI_OK;
}

uint8_t BSP_QSPI_Config(void)
{
  uint8_t reg = 0;


  if (QSPI_ReadStatus(&hqspi, READ_STATUS_REG2_CMD, &reg) != QSPI_OK)
  {
    return QSPI_ERROR;
  }

  // QUAD MODE Enable
  if ((reg & (1<<1)) == 0x00)
  {
    reg |= (1<<1);
    if (QSPI_WriteStatus(&hqspi, WRITE_STATUS_REG2_CMD, reg) != QSPI_OK)
    {
      return QSPI_ERROR;
    }
  }


  return QSPI_OK;
}

uint8_t BSP_QSPI_DeInit(void)
{
  hqspi.Instance = OCTOSPI1;

  /* Call the DeInit function to reset the driver */
  if (HAL_OSPI_DeInit(&hqspi) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  return QSPI_OK;
}

uint8_t BSP_QSPI_Read(uint8_t* p_data, uint32_t addr, uint32_t length)
{
  OSPI_RegularCmdTypeDef s_command = {0};


  /* Initialize the read command */
  s_command.OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG;
  s_command.FlashId            = HAL_OSPI_FLASH_ID_1;
  
  s_command.InstructionMode    = HAL_OSPI_INSTRUCTION_1_LINE;
  s_command.InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE;
  s_command.InstructionSize    = HAL_OSPI_INSTRUCTION_8_BITS;
  s_command.Instruction        = QUAD_INOUT_FAST_READ_CMD;
  
  s_command.AddressMode        = HAL_OSPI_ADDRESS_4_LINES;
  s_command.AddressDtrMode     = HAL_OSPI_ADDRESS_DTR_DISABLE;
  s_command.AddressSize        = HAL_OSPI_ADDRESS_24_BITS;
  s_command.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_4_LINES;
  s_command.AlternateBytesSize = HAL_OSPI_ALTERNATE_BYTES_8_BITS;
  s_command.AlternateBytes     = 0;

  s_command.DataMode           = HAL_OSPI_DATA_4_LINES;
  s_command.DataDtrMode        = HAL_OSPI_DATA_DTR_DISABLE;
  s_command.DummyCycles        = W25Q128FV_DUMMY_CYCLES_READ_QUAD;
  s_command.DQSMode            = HAL_OSPI_DQS_DISABLE;
  s_command.SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD;

  s_command.Address            = addr;
  s_command.NbData             = length;


  /* Send the command */
  if (HAL_OSPI_Command(&hqspi, &s_command, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return false;
  }

  /* Reception of the data */
  if (HAL_OSPI_Receive(&hqspi, p_data, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return false;
  }

  return QSPI_OK;
}

uint8_t BSP_QSPI_Write(uint8_t* p_data, uint32_t addr, uint32_t length)
{
  OSPI_RegularCmdTypeDef s_command = {0};
  uint32_t end_addr, current_size, current_addr;

  /* Calculation of the size between the write address and the end of the page */
  current_size = W25Q128FV_PAGE_SIZE - (addr % W25Q128FV_PAGE_SIZE);

  /* Check if the size of the data is less than the remaining place in the page */
  if (current_size > length)
  {
    current_size = length;
  }

  /* Initialize the adress variables */
  current_addr = addr;
  end_addr = addr + length;

  /* Initialize the program command */
  s_command.OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG;
  s_command.FlashId            = HAL_OSPI_FLASH_ID_1;
  s_command.InstructionMode    = HAL_OSPI_INSTRUCTION_1_LINE;
  s_command.InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE;
  s_command.InstructionSize    = HAL_OSPI_INSTRUCTION_8_BITS;
  s_command.Instruction        = QUAD_IN_FAST_PROG_CMD;
  
  s_command.AddressMode        = HAL_OSPI_ADDRESS_1_LINE;
  s_command.AddressDtrMode     = HAL_OSPI_ADDRESS_DTR_DISABLE;
  s_command.AddressSize        = HAL_OSPI_ADDRESS_24_BITS;
  
  s_command.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
  
  s_command.DataMode           = HAL_OSPI_DATA_4_LINES;
  s_command.DataDtrMode        = HAL_OSPI_DATA_DTR_DISABLE;
  s_command.DummyCycles        = 0;
  s_command.DQSMode            = HAL_OSPI_DQS_DISABLE;
  s_command.SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD;

  /* Perform the write page by page */
  do
  {
    s_command.Address = current_addr;
    s_command.NbData  = current_size;

    /* Enable write operations */
    if (QSPI_WriteEnable(&hqspi) != QSPI_OK)
    {
      return QSPI_ERROR;
    }

    /* Configure the command */
    if (HAL_OSPI_Command(&hqspi, &s_command, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
      return QSPI_ERROR;
    }

    /* Transmission of the data */
    if (HAL_OSPI_Transmit(&hqspi, p_data, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
      return QSPI_ERROR;
    }

    /* Configure automatic polling mode to wait for end of program */
    if (QSPI_AutoPollingMemReady(&hqspi, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != QSPI_OK)
    {
      return QSPI_ERROR;
    }

    /* Update the address and size variables for next page programming */
    current_addr += current_size;
    p_data += current_size;
    current_size = ((current_addr + W25Q128FV_PAGE_SIZE) > end_addr) ? (end_addr - current_addr) : W25Q128FV_PAGE_SIZE;
  } while (current_addr < end_addr);

  return QSPI_OK;
}

uint8_t BSP_QSPI_Erase_Block(uint32_t BlockAddress)
{
  OSPI_RegularCmdTypeDef s_command = {0};

  /* Initialize the erase command */
  s_command.OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG;
  s_command.FlashId            = HAL_OSPI_FLASH_ID_1;
  s_command.InstructionMode    = HAL_OSPI_INSTRUCTION_1_LINE;
  s_command.InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE;
  s_command.InstructionSize    = HAL_OSPI_INSTRUCTION_8_BITS;
  s_command.Instruction        = SUBSECTOR_ERASE_CMD;
  
  s_command.AddressMode        = HAL_OSPI_ADDRESS_1_LINE;
  s_command.AddressDtrMode     = HAL_OSPI_ADDRESS_DTR_DISABLE;
  s_command.AddressSize        = HAL_OSPI_ADDRESS_24_BITS;
  s_command.Address            = BlockAddress;
  s_command.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
  
  s_command.DataMode           = HAL_OSPI_DATA_NONE;
  s_command.DataDtrMode        = HAL_OSPI_DATA_DTR_DISABLE;
  s_command.DummyCycles        = 0;
  s_command.DQSMode            = HAL_OSPI_DQS_DISABLE;
  s_command.SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD;


  /* Enable write operations */
  if (QSPI_WriteEnable(&hqspi) != QSPI_OK)
  {
    return QSPI_ERROR;
  }

  /* Send the command */
  if (HAL_OSPI_Command(&hqspi, &s_command, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  /* Configure automatic polling mode to wait for end of erase */
  if (QSPI_AutoPollingMemReady(&hqspi, W25Q128FV_SUBSECTOR_ERASE_MAX_TIME) != QSPI_OK)
  {
    return QSPI_ERROR;
  }

  return QSPI_OK;
}

uint8_t BSP_QSPI_Erase_Sector(uint32_t SectorAddress)
{
  OSPI_RegularCmdTypeDef s_command = {0};

  /* Initialize the erase command */
  s_command.OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG;
  s_command.FlashId            = HAL_OSPI_FLASH_ID_1;
  s_command.InstructionMode    = HAL_OSPI_INSTRUCTION_1_LINE;
  s_command.InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE;
  s_command.InstructionSize    = HAL_OSPI_INSTRUCTION_8_BITS;
  s_command.Instruction        = SECTOR_ERASE_CMD;
  
  s_command.AddressMode        = HAL_OSPI_ADDRESS_1_LINE;
  s_command.AddressDtrMode     = HAL_OSPI_ADDRESS_DTR_DISABLE;
  s_command.AddressSize        = HAL_OSPI_ADDRESS_24_BITS;
  s_command.Address            = SectorAddress;

  s_command.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
  
  s_command.DataMode           = HAL_OSPI_DATA_NONE;
  s_command.DataDtrMode        = HAL_OSPI_DATA_DTR_DISABLE;
  s_command.DummyCycles        = 0;
  s_command.DQSMode            = HAL_OSPI_DQS_DISABLE;
  s_command.SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD;


  /* Enable write operations */
  if (QSPI_WriteEnable(&hqspi) != QSPI_OK)
  {
    return QSPI_ERROR;
  }

  /* Send the command */
  if (HAL_OSPI_Command(&hqspi, &s_command, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  /* Configure automatic polling mode to wait for end of erase */
  if (QSPI_AutoPollingMemReady(&hqspi, W25Q128FV_SUBSECTOR_ERASE_MAX_TIME) != QSPI_OK)
  {
    return QSPI_ERROR;
  }

  return QSPI_OK;
}

uint8_t BSP_QSPI_Erase_Chip(void)
{
  OSPI_RegularCmdTypeDef s_command = {0};

  /* Initialize the erase command */
  s_command.OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG;
  s_command.FlashId            = HAL_OSPI_FLASH_ID_1;
  s_command.InstructionMode    = HAL_OSPI_INSTRUCTION_1_LINE;
  s_command.InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE;
  s_command.InstructionSize    = HAL_OSPI_INSTRUCTION_8_BITS;
  s_command.Instruction        = BULK_ERASE_CMD;
  
  s_command.AddressMode        = HAL_OSPI_ADDRESS_NONE;
  s_command.AddressDtrMode     = HAL_OSPI_ADDRESS_DTR_DISABLE;
  s_command.AddressSize        = HAL_OSPI_ADDRESS_24_BITS;
  
  s_command.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
  
  s_command.DataMode           = HAL_OSPI_DATA_NONE;
  s_command.DataDtrMode        = HAL_OSPI_DATA_DTR_DISABLE;
  s_command.DummyCycles        = 0;
  s_command.DQSMode            = HAL_OSPI_DQS_DISABLE;
  s_command.SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD;

  /* Enable write operations */
  if (QSPI_WriteEnable(&hqspi) != QSPI_OK)
  {
    return QSPI_ERROR;
  }

  /* Send the command */
  if (HAL_OSPI_Command(&hqspi, &s_command, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  /* Configure automatic polling mode to wait for end of erase */
  if (QSPI_AutoPollingMemReady(&hqspi, W25Q128FV_BULK_ERASE_MAX_TIME) != QSPI_OK)
  {
    return QSPI_ERROR;
  }

  return QSPI_OK;
}

uint8_t BSP_QSPI_GetStatus(void)
{
  OSPI_RegularCmdTypeDef s_command = {0};
  uint8_t reg;

  /* Initialize the read flag status register command */
  s_command.OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG;
  s_command.FlashId            = HAL_OSPI_FLASH_ID_1;
  s_command.InstructionMode    = HAL_OSPI_INSTRUCTION_1_LINE;
  s_command.InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE;
  s_command.InstructionSize    = HAL_OSPI_INSTRUCTION_8_BITS;
  s_command.Instruction        = READ_FLAG_STATUS_REG_CMD;
  
  s_command.AddressMode        = HAL_OSPI_ADDRESS_NONE;
  s_command.AddressDtrMode     = HAL_OSPI_ADDRESS_DTR_DISABLE;
  s_command.AddressSize        = HAL_OSPI_ADDRESS_24_BITS;
  
  s_command.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
  
  s_command.DataMode           = HAL_OSPI_DATA_1_LINE;
  s_command.DataDtrMode        = HAL_OSPI_DATA_DTR_DISABLE;
  s_command.DummyCycles        = 0;
  s_command.DQSMode            = HAL_OSPI_DQS_DISABLE;
  s_command.SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD;

  s_command.NbData             = 1;

  /* Configure the command */
  if (HAL_OSPI_Command(&hqspi, &s_command, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  /* Reception of the data */
  if (HAL_OSPI_Receive(&hqspi, &reg, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  /* Check the value of the register */
  if ((reg & (W25Q128FV_FSR_PRERR | W25Q128FV_FSR_VPPERR | W25Q128FV_FSR_PGERR | W25Q128FV_FSR_ERERR)) != 0)
  {
    return QSPI_ERROR;
  }
  else if ((reg & (W25Q128FV_FSR_PGSUS | W25Q128FV_FSR_ERSUS)) != 0)
  {
    return QSPI_SUSPENDED;
  }
  else if ((reg & W25Q128FV_FSR_READY) != 0)
  {
    return QSPI_OK;
  }
  else
  {
    return QSPI_BUSY;
  }
}

uint8_t BSP_QSPI_GetID(QSPI_Info* p_info)
{
  OSPI_RegularCmdTypeDef s_command = {0};


  /* Initialize the read flag status register command */
  s_command.OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG;
  s_command.FlashId            = HAL_OSPI_FLASH_ID_1;
  s_command.InstructionMode    = HAL_OSPI_INSTRUCTION_1_LINE;
  s_command.InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE;
  s_command.InstructionSize    = HAL_OSPI_INSTRUCTION_8_BITS;
  s_command.Instruction        = READ_ID_CMD;
  
  s_command.AddressMode        = HAL_OSPI_ADDRESS_NONE;
  s_command.AddressDtrMode     = HAL_OSPI_ADDRESS_DTR_DISABLE;
  s_command.AddressSize        = HAL_OSPI_ADDRESS_24_BITS;
  
  s_command.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
  
  s_command.DataMode           = HAL_OSPI_DATA_1_LINE;
  s_command.DataDtrMode        = HAL_OSPI_DATA_DTR_DISABLE;
  s_command.DummyCycles        = 0;
  s_command.DQSMode            = HAL_OSPI_DQS_DISABLE;
  s_command.SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD;

  s_command.NbData             = 20;
  
  /* Configure the command */
  if (HAL_OSPI_Command(&hqspi, &s_command, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  /* Reception of the data */
  if (HAL_OSPI_Receive(&hqspi, p_info->device_id, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  return QSPI_OK;
}

uint8_t BSP_QSPI_GetInfo(QSPI_Info* p_info)
{
  /* Configure the structure with the memory configuration */
  p_info->FlashSize          = W25Q128FV_FLASH_SIZE;
  p_info->EraseSectorSize    = W25Q128FV_SUBSECTOR_SIZE;
  p_info->EraseSectorsNumber = (W25Q128FV_FLASH_SIZE/W25Q128FV_SUBSECTOR_SIZE);
  p_info->ProgPageSize       = W25Q128FV_PAGE_SIZE;
  p_info->ProgPagesNumber    = (W25Q128FV_FLASH_SIZE/W25Q128FV_PAGE_SIZE);

  return QSPI_OK;
}

uint8_t BSP_QSPI_EnableMemoryMappedMode(void)
{
  OSPI_RegularCmdTypeDef   s_command = {0};
  OSPI_MemoryMappedTypeDef s_mem_mapped_cfg = {0};

  /* Configure the command for the read instruction */
  s_command.OperationType      = HAL_OSPI_OPTYPE_READ_CFG;
  s_command.FlashId            = HAL_OSPI_FLASH_ID_1;
  s_command.InstructionMode    = HAL_OSPI_INSTRUCTION_1_LINE;
  s_command.InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE;
  s_command.InstructionSize    = HAL_OSPI_INSTRUCTION_8_BITS;
  s_command.Instruction        = QUAD_INOUT_FAST_READ_CMD;
  
  s_command.AddressMode        = HAL_OSPI_ADDRESS_4_LINES;
  s_command.AddressDtrMode     = HAL_OSPI_ADDRESS_DTR_DISABLE;
  s_command.AddressSize        = HAL_OSPI_ADDRESS_24_BITS;
  
  s_command.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_4_LINES;
  s_command.AlternateBytesSize = HAL_OSPI_ALTERNATE_BYTES_8_BITS;
  s_command.AlternateBytes     = (1<<5);
  
  s_command.DataMode           = HAL_OSPI_DATA_4_LINES;
  s_command.DataDtrMode        = HAL_OSPI_DATA_DTR_DISABLE;
  s_command.DummyCycles        = W25Q128FV_DUMMY_CYCLES_READ_QUAD;
  s_command.DQSMode            = HAL_OSPI_DQS_DISABLE;
  s_command.SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD;


  /* Configure the command */
  if (HAL_OSPI_Command(&hqspi, &s_command, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  /* Configure the memory mapped mode */
  s_mem_mapped_cfg.TimeOutActivation = HAL_OSPI_TIMEOUT_COUNTER_DISABLE;

  if (HAL_OSPI_MemoryMapped(&hqspi, &s_mem_mapped_cfg) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  return QSPI_OK;
}

static uint8_t QSPI_ResetMemory(OSPI_HandleTypeDef *p_hqspi)
{
  OSPI_RegularCmdTypeDef s_command = {0};

  /* Initialize the reset enable command */
  s_command.OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG;
  s_command.FlashId            = HAL_OSPI_FLASH_ID_1;
  s_command.InstructionMode    = HAL_OSPI_INSTRUCTION_1_LINE;
  s_command.InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE;
  s_command.InstructionSize    = HAL_OSPI_INSTRUCTION_8_BITS;
  s_command.Instruction        = RESET_ENABLE_CMD;
  
  s_command.AddressMode        = HAL_OSPI_ADDRESS_NONE;
  s_command.AddressDtrMode     = HAL_OSPI_ADDRESS_DTR_DISABLE;
  s_command.AddressSize        = HAL_OSPI_ADDRESS_24_BITS;
  
  s_command.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
  
  s_command.DataMode           = HAL_OSPI_DATA_NONE;
  s_command.DataDtrMode        = HAL_OSPI_DATA_DTR_DISABLE;
  s_command.DummyCycles        = 0;
  s_command.DQSMode            = HAL_OSPI_DQS_DISABLE;
  s_command.SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD;

  /* Send the command */
  if (HAL_OSPI_Command(p_hqspi, &s_command, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  /* Send the reset memory command */
  s_command.Instruction = RESET_MEMORY_CMD;
  if (HAL_OSPI_Command(p_hqspi, &s_command, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  /* Configure automatic polling mode to wait the memory is ready */
  if (QSPI_AutoPollingMemReady(p_hqspi, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != QSPI_OK)
  {
    return QSPI_ERROR;
  }

  return QSPI_OK;
}

static uint8_t QSPI_WriteEnable(OSPI_HandleTypeDef *p_hqspi)
{
  OSPI_RegularCmdTypeDef  s_command = {0};
  OSPI_AutoPollingTypeDef s_config = {0};

  /* Enable write operations */
  s_command.OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG;
  s_command.FlashId            = HAL_OSPI_FLASH_ID_1;
  s_command.InstructionMode    = HAL_OSPI_INSTRUCTION_1_LINE;
  s_command.InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE;
  s_command.InstructionSize    = HAL_OSPI_INSTRUCTION_8_BITS;
  s_command.Instruction        = WRITE_ENABLE_CMD;
  
  s_command.AddressMode        = HAL_OSPI_ADDRESS_NONE;
  s_command.AddressDtrMode     = HAL_OSPI_ADDRESS_DTR_DISABLE;
  s_command.AddressSize        = HAL_OSPI_ADDRESS_24_BITS;
  
  s_command.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
  
  s_command.DataMode           = HAL_OSPI_DATA_NONE;
  s_command.DataDtrMode        = HAL_OSPI_DATA_DTR_DISABLE;
  s_command.DummyCycles        = 0;
  s_command.DQSMode            = HAL_OSPI_DQS_DISABLE;
  s_command.SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD;
  s_command.NbData             = 0;


  if (HAL_OSPI_Command(p_hqspi, &s_command, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  s_command.Instruction    = READ_STATUS_REG_CMD;
  s_command.DataMode       = HAL_OSPI_DATA_1_LINE;
  s_command.NbData         = 1;
  if (HAL_OSPI_Command(p_hqspi, &s_command, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  /* Configure automatic polling mode to wait for write enabling */
  s_config.Match           = W25Q128FV_SR_WREN;
  s_config.Mask            = W25Q128FV_SR_WREN;
  s_config.MatchMode       = HAL_OSPI_MATCH_MODE_AND;
  s_config.Interval        = 0x10;
  s_config.AutomaticStop   = HAL_OSPI_AUTOMATIC_STOP_ENABLE;

  if (HAL_OSPI_AutoPolling(p_hqspi, &s_config, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  return QSPI_OK;
}

static uint8_t QSPI_AutoPollingMemReady(OSPI_HandleTypeDef *p_hqspi, uint32_t Timeout)
{
  OSPI_RegularCmdTypeDef  s_command = {0};
  OSPI_AutoPollingTypeDef s_config = {0};

  /* Configure automatic polling mode to wait for memory ready */
  s_command.OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG;
  s_command.FlashId            = HAL_OSPI_FLASH_ID_1;
  s_command.InstructionMode    = HAL_OSPI_INSTRUCTION_1_LINE;
  s_command.InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE;
  s_command.InstructionSize    = HAL_OSPI_INSTRUCTION_8_BITS;
  s_command.Instruction        = READ_STATUS_REG_CMD;
  
  s_command.AddressMode        = HAL_OSPI_ADDRESS_NONE;
  s_command.AddressDtrMode     = HAL_OSPI_ADDRESS_DTR_DISABLE;
  s_command.AddressSize        = HAL_OSPI_ADDRESS_24_BITS;
  
  s_command.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
  
  s_command.DataMode           = HAL_OSPI_DATA_1_LINE;
  s_command.DataDtrMode        = HAL_OSPI_DATA_DTR_DISABLE;
  s_command.DummyCycles        = 0;
  s_command.DQSMode            = HAL_OSPI_DQS_DISABLE;
  s_command.SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD;
  s_command.NbData             = 1;


  if (HAL_OSPI_Command(p_hqspi, &s_command, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return false;
  }

  s_config.Match           = 0;
  s_config.Mask            = W25Q128FV_SR_WIP;
  s_config.MatchMode       = HAL_OSPI_MATCH_MODE_AND;
  s_config.Interval        = 0x10;
  s_config.AutomaticStop   = HAL_OSPI_AUTOMATIC_STOP_ENABLE;

  if (HAL_OSPI_AutoPolling(p_hqspi, &s_config, Timeout) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  return QSPI_OK;
}

static uint8_t QSPI_ReadStatus(OSPI_HandleTypeDef *p_hqspi, uint8_t cmd, uint8_t *p_data)
{
  OSPI_RegularCmdTypeDef s_command = {0};
  uint8_t reg;

  /* Initialize the read flag status register command */
  s_command.OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG;
  s_command.FlashId            = HAL_OSPI_FLASH_ID_1;
  s_command.InstructionMode    = HAL_OSPI_INSTRUCTION_1_LINE;
  s_command.InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE;
  s_command.InstructionSize    = HAL_OSPI_INSTRUCTION_8_BITS;
  s_command.Instruction        = cmd;
  
  s_command.AddressMode        = HAL_OSPI_ADDRESS_NONE;
  s_command.AddressDtrMode     = HAL_OSPI_ADDRESS_DTR_DISABLE;
  s_command.AddressSize        = HAL_OSPI_ADDRESS_24_BITS;
  
  s_command.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
  
  s_command.DataMode           = HAL_OSPI_DATA_1_LINE;
  s_command.DataDtrMode        = HAL_OSPI_DATA_DTR_DISABLE;
  s_command.DummyCycles        = 0;
  s_command.DQSMode            = HAL_OSPI_DQS_DISABLE;
  s_command.SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD;
  s_command.NbData             = 1;


  /* Configure the command */
  if (HAL_OSPI_Command(p_hqspi, &s_command, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  /* Reception of the data */
  if (HAL_OSPI_Receive(p_hqspi, &reg, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  *p_data = reg;

  return QSPI_OK;
}

static uint8_t QSPI_WriteStatus(OSPI_HandleTypeDef *p_hqspi, uint8_t cmd, uint8_t data)
{
  OSPI_RegularCmdTypeDef s_command = {0};

  /* Initialize the program command */
  s_command.OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG;
  s_command.FlashId            = HAL_OSPI_FLASH_ID_1;
  s_command.InstructionMode    = HAL_OSPI_INSTRUCTION_1_LINE;
  s_command.InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE;
  s_command.InstructionSize    = HAL_OSPI_INSTRUCTION_8_BITS;
  s_command.Instruction        = cmd;
  
  s_command.AddressMode        = HAL_OSPI_ADDRESS_NONE;
  s_command.AddressDtrMode     = HAL_OSPI_ADDRESS_DTR_DISABLE;
  s_command.AddressSize        = HAL_OSPI_ADDRESS_24_BITS;
  
  s_command.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
  
  s_command.DataMode           = HAL_OSPI_DATA_1_LINE;
  s_command.DataDtrMode        = HAL_OSPI_DATA_DTR_DISABLE;
  s_command.DummyCycles        = 0;
  s_command.DQSMode            = HAL_OSPI_DQS_DISABLE;
  s_command.SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD;
  s_command.NbData             = 1;

  /* Enable write operations */
  if (QSPI_WriteEnable(p_hqspi) != QSPI_OK)
  {
    return QSPI_ERROR;
  }

  /* Configure the command */
  if (HAL_OSPI_Command(p_hqspi, &s_command, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  /* Transmission of the data */
  if (HAL_OSPI_Transmit(p_hqspi, &data, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  /* Configure automatic polling mode to wait for end of program */
  if (QSPI_AutoPollingMemReady(p_hqspi, HAL_OSPI_TIMEOUT_DEFAULT_VALUE) != QSPI_OK)
  {
    return QSPI_ERROR;
  }


  return QSPI_OK;
}






void HAL_OSPI_MspInit(OSPI_HandleTypeDef* ospiHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  if(ospiHandle->Instance==OCTOSPI1)
  {
  /** Initializes the peripherals clock
  */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_OSPI;
    PeriphClkInitStruct.OspiClockSelection = RCC_OSPICLKSOURCE_PLL;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    /* OCTOSPI1 clock enable */
    __HAL_RCC_OCTOSPIM_CLK_ENABLE();
    __HAL_RCC_OSPI1_CLK_ENABLE();

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    /**OCTOSPI1 GPIO Configuration
    PB2     ------> OCTOSPIM_P1_CLK
    PC11     ------> OCTOSPIM_P1_NCS
    PD4     ------> OCTOSPIM_P1_IO4
    PD5     ------> OCTOSPIM_P1_IO5
    PD6     ------> OCTOSPIM_P1_IO6
    PG14     ------> OCTOSPIM_P1_IO7
    */
    GPIO_InitStruct.Pin = GPIO_PIN_2;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF9_OCTOSPIM_P1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_11;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF9_OCTOSPIM_P1;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF10_OCTOSPIM_P1;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_14;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF9_OCTOSPIM_P1;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);
  }
}

void HAL_OSPI_MspDeInit(OSPI_HandleTypeDef* ospiHandle)
{

  if(ospiHandle->Instance==OCTOSPI1)
  {
    /* Peripheral clock disable */
    __HAL_RCC_OCTOSPIM_CLK_DISABLE();
    __HAL_RCC_OSPI1_CLK_DISABLE();

    /**OCTOSPI1 GPIO Configuration
    PB2     ------> OCTOSPIM_P1_CLK
    PC11     ------> OCTOSPIM_P1_NCS
    PD4     ------> OCTOSPIM_P1_IO4
    PD5     ------> OCTOSPIM_P1_IO5
    PD6     ------> OCTOSPIM_P1_IO6
    PG14     ------> OCTOSPIM_P1_IO7
    */
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_2);

    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_11);

    HAL_GPIO_DeInit(GPIOD, GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6);

    HAL_GPIO_DeInit(GPIOG, GPIO_PIN_14);
  }
}
