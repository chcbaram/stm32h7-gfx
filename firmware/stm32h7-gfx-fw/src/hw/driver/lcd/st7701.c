#include "lcd/st7701.h"
#include "spi.h"
#include "gpio.h"


static bool st7701Reset(void);
static bool st7701InitRegs(void);
static bool st7701WriteCmd(uint8_t data);
static bool st7701WriteData(uint8_t data);


static bool is_init = false;
static uint8_t spi_ch = _DEF_SPI2;



bool st7701Init(void)
{
  bool ret;

  spiBegin(spi_ch);
  spiSetDataMode(spi_ch, SPI_MODE0);

  ret = st7701Reset();

  is_init = ret;
  return ret;
}

bool st7701Reset(void)
{
  st7701InitRegs();
  return true;
}

bool st7701InitRegs(void)
{
  //PAGE1
  st7701WriteCmd(0xFF);    
  st7701WriteData(0x77);
  st7701WriteData(0x01);
  st7701WriteData(0x00);
  st7701WriteData(0x00);
  st7701WriteData(0x10);

  st7701WriteCmd(0xC0);    
  st7701WriteData(0x3B);
  st7701WriteData(0x00);

  st7701WriteCmd(0xC1);    
  st7701WriteData(0x0D);
  st7701WriteData(0x02);

  st7701WriteCmd(0xC2);    
  st7701WriteData(0x31);
  st7701WriteData(0x05);

  st7701WriteCmd(0xCD);
  st7701WriteData(0x08);

  st7701WriteCmd(0xB0);    
  st7701WriteData(0x00); //Positive Voltage Gamma Control
  st7701WriteData(0x11);
  st7701WriteData(0x18);
  st7701WriteData(0x0E);
  st7701WriteData(0x11);
  st7701WriteData(0x06);
  st7701WriteData(0x07);
  st7701WriteData(0x08);
  st7701WriteData(0x07);
  st7701WriteData(0x22);
  st7701WriteData(0x04);
  st7701WriteData(0x12);
  st7701WriteData(0x0F);
  st7701WriteData(0xAA);
  st7701WriteData(0x31);
  st7701WriteData(0x18);

  st7701WriteCmd(0xB1);    
  st7701WriteData(0x00); //Negative Voltage Gamma Control
  st7701WriteData(0x11);
  st7701WriteData(0x19);
  st7701WriteData(0x0E);
  st7701WriteData(0x12);
  st7701WriteData(0x07);
  st7701WriteData(0x08);
  st7701WriteData(0x08);
  st7701WriteData(0x08);
  st7701WriteData(0x22);
  st7701WriteData(0x04);
  st7701WriteData(0x11);
  st7701WriteData(0x11);
  st7701WriteData(0xA9);
  st7701WriteData(0x32);
  st7701WriteData(0x18);

  //PAGE1
  st7701WriteCmd(0xFF);    
  st7701WriteData(0x77);
  st7701WriteData(0x01);
  st7701WriteData(0x00);
  st7701WriteData(0x00);
  st7701WriteData(0x11);

  st7701WriteCmd(0xB0);    st7701WriteData(0x60); //Vop=4.7375v
  st7701WriteCmd(0xB1);    st7701WriteData(0x32); //VCOM=32
  st7701WriteCmd(0xB2);    st7701WriteData(0x07); //VGH=15v
  st7701WriteCmd(0xB3);    st7701WriteData(0x80);
  st7701WriteCmd(0xB5);    st7701WriteData(0x49); //VGL=-10.17v
  st7701WriteCmd(0xB7);    st7701WriteData(0x85);
  st7701WriteCmd(0xB8);    st7701WriteData(0x21); //AVDD=6.6 & AVCL=-4.6
  st7701WriteCmd(0xC1);    st7701WriteData(0x78);
  st7701WriteCmd(0xC2);    st7701WriteData(0x78);

  st7701WriteCmd(0xE0);    
  st7701WriteData(0x00);
  st7701WriteData(0x1B);
  st7701WriteData(0x02);

  st7701WriteCmd(0xE1);   
  st7701WriteData(0x08);
  st7701WriteData(0xA0);
  st7701WriteData(0x00);
  st7701WriteData(0x00);
  st7701WriteData(0x07);
  st7701WriteData(0xA0);
  st7701WriteData(0x00);
  st7701WriteData(0x00);
  st7701WriteData(0x00);
  st7701WriteData(0x44);
  st7701WriteData(0x44);

  st7701WriteCmd(0xE2);    
  st7701WriteData(0x11);
  st7701WriteData(0x11);
  st7701WriteData(0x44);
  st7701WriteData(0x44);
  st7701WriteData(0xED);
  st7701WriteData(0xA0);
  st7701WriteData(0x00);
  st7701WriteData(0x00);
  st7701WriteData(0xEC);
  st7701WriteData(0xA0);
  st7701WriteData(0x00);
  st7701WriteData(0x00);

  st7701WriteCmd(0xE3);    
  st7701WriteData(0x00);
  st7701WriteData(0x00);
  st7701WriteData(0x11);
  st7701WriteData(0x11);

  st7701WriteCmd(0xE4);    
  st7701WriteData(0x44);
  st7701WriteData(0x44);

  st7701WriteCmd(0xE5);    
  st7701WriteData(0x0A);
  st7701WriteData(0xE9);
  st7701WriteData(0xD8);
  st7701WriteData(0xA0);
  st7701WriteData(0x0C);
  st7701WriteData(0xEB);
  st7701WriteData(0xD8);
  st7701WriteData(0xA0);
  st7701WriteData(0x0E);
  st7701WriteData(0xED);
  st7701WriteData(0xD8);
  st7701WriteData(0xA0);
  st7701WriteData(0x10);
  st7701WriteData(0xEF);
  st7701WriteData(0xD8);
  st7701WriteData(0xA0);

  st7701WriteCmd(0xE6);   
  st7701WriteData(0x00);
  st7701WriteData(0x00);
  st7701WriteData(0x11);
  st7701WriteData(0x11);

  st7701WriteCmd(0xE7);    
  st7701WriteData(0x44);
  st7701WriteData(0x44);

  st7701WriteCmd(0xE8);    
  st7701WriteData(0x09);
  st7701WriteData(0xE8);
  st7701WriteData(0xD8);
  st7701WriteData(0xA0);
  st7701WriteData(0x0B);
  st7701WriteData(0xEA);
  st7701WriteData(0xD8);
  st7701WriteData(0xA0);
  st7701WriteData(0x0D);
  st7701WriteData(0xEC);
  st7701WriteData(0xD8);
  st7701WriteData(0xA0);
  st7701WriteData(0x0F);
  st7701WriteData(0xEE);
  st7701WriteData(0xD8);
  st7701WriteData(0xA0);

  st7701WriteCmd(0xEB);    
  st7701WriteData(0x02);
  st7701WriteData(0x00);
  st7701WriteData(0xE4);
  st7701WriteData(0xE4);
  st7701WriteData(0x88);
  st7701WriteData(0x00);
  st7701WriteData(0x40);

  st7701WriteCmd(0xEC);    
  st7701WriteData(0x3C);
  st7701WriteData(0x00);

  st7701WriteCmd(0xED);    
  st7701WriteData(0xAB);
  st7701WriteData(0x89);
  st7701WriteData(0x76);
  st7701WriteData(0x54);
  st7701WriteData(0x02);
  st7701WriteData(0xFF);
  st7701WriteData(0xFF);
  st7701WriteData(0xFF);
  st7701WriteData(0xFF);
  st7701WriteData(0xFF);
  st7701WriteData(0xFF);
  st7701WriteData(0x20);
  st7701WriteData(0x45);
  st7701WriteData(0x67);
  st7701WriteData(0x98);
  st7701WriteData(0xBA);

  st7701WriteCmd(0x36);    
  st7701WriteData(0x00);

  //-----------VAP & VAN---------------
  st7701WriteCmd(0xFF);    
  st7701WriteData(0x77);
  st7701WriteData(0x01);
  st7701WriteData(0x00);
  st7701WriteData(0x00);
  st7701WriteData(0x13);

  st7701WriteCmd(0xE5);    
  st7701WriteData(0xE4);

  st7701WriteCmd(0xFF);
  st7701WriteData(0x77);
  st7701WriteData(0x01);
  st7701WriteData(0x00);
  st7701WriteData(0x00);
  st7701WriteData(0x00);


  st7701WriteCmd(0x3A);   //0x70 RGB888, 0x60 RGB666, 0x50 RGB565
  st7701WriteData(0x60);

  st7701WriteCmd(0x21);   //Display Inversion On
  
  st7701WriteCmd(0x11);   //Sleep Out
  delay(100);

  bool ret;

  ret = st7701WriteCmd(0x29);   //Display On
  delay(50);

  return ret;
}

bool st7701WriteCmd(uint8_t data)
{
  bool ret;
  uint16_t tx_data;

  gpioPinWrite(_PIN_GPIO_LCD_SPI_CS, _DEF_LOW);

  tx_data = (0<<8) | data;
  spiSetBitWidth(spi_ch, 9);
  ret = spiTransfer(spi_ch, (uint8_t *)&tx_data, NULL, 1, 10);

  gpioPinWrite(_PIN_GPIO_LCD_SPI_CS, _DEF_HIGH);
  return ret;
}

bool st7701WriteData(uint8_t data)
{
  bool ret;
  uint16_t tx_data;

  gpioPinWrite(_PIN_GPIO_LCD_SPI_CS, _DEF_LOW);

  tx_data = (1<<8) | data;
  spiSetBitWidth(spi_ch, 9);
  ret = spiTransfer(spi_ch, (uint8_t *)&tx_data, NULL, 1, 10);

  gpioPinWrite(_PIN_GPIO_LCD_SPI_CS, _DEF_HIGH);
  return ret;
}
