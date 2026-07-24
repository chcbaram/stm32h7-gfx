#include "driver/drv_uart.h"


typedef struct
{
  uint8_t  ch;
  uint32_t baud;
} drv_uart_args_t;

static bool open_(void *args);
static bool close_(void *args);  
static uint32_t available(void *args);
static bool flush(void *args);
static uint8_t read(void *args);
static uint32_t write(void *args, uint8_t *p_data, uint32_t length);  



bool drvUartInit(cmd_driver_t *p_driver, uint8_t ch, uint32_t baud)
{
  drv_uart_args_t *p_args = (drv_uart_args_t *)p_driver->args;

  p_args->ch = ch;
  p_args->baud = baud;

  p_driver->open = open_;
  p_driver->close = close_;
  p_driver->available = available;
  p_driver->flush = flush;
  p_driver->read = read;
  p_driver->write= write;

  return true;
}

bool open_(void *args)
{
  drv_uart_args_t *p_args = (drv_uart_args_t *)args;

  return uartOpen(p_args->ch, p_args->baud);
}

bool close_(void *args)
{
  drv_uart_args_t *p_args = (drv_uart_args_t *)args;

  return uartClose(p_args->ch);
}

uint32_t available(void *args)
{
  drv_uart_args_t *p_args = (drv_uart_args_t *)args;

  return uartAvailable(p_args->ch);
}

bool flush(void *args)
{
  drv_uart_args_t *p_args = (drv_uart_args_t *)args;

  return uartFlush(p_args->ch);
}

uint8_t read(void *args)
{
  drv_uart_args_t *p_args = (drv_uart_args_t *)args;

  return uartRead(p_args->ch);
}

uint32_t write(void *args, uint8_t *p_data, uint32_t length)
{
  drv_uart_args_t *p_args = (drv_uart_args_t *)args;

  return uartWrite(p_args->ch, p_data, length);
}
