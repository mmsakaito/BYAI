#include <stdint.h>




#include "st7789.h"
#include "driver/spi_master.h"
#include "kernel.h"
#include "sysdef.h"
#include "driver/gpio.h"
/*port相关*/
inline void LCD_RES_Clr() {

}
inline void LCD_RES_Set(){
}
inline void LCD_DC_Clr() {
	gpio_set_level(PIN_SPI2_RS,0);
}
inline void LCD_DC_Set(){
	gpio_set_level(PIN_SPI2_RS,1);
}
inline void LCD_CS_Clr() {
	gpio_set_level(PIN_SPI2_CS,0);
}
inline void LCD_CS_Set() {
	gpio_set_level(PIN_SPI2_CS,1);
}
inline void LCD_BLK_Clr() {

}
inline void LCD_BLK_Set() {
}
/******************************************************************************
      函数说明：LCD写入数据
      入口数据：dat 写入的数据
      返回值：  无
******************************************************************************/
void LCD_WR_DATA8(uint8_t dat)
{
	spi_transaction_t t={
		.length = 8,
		.tx_buffer = &dat,
	};
	spi_device_polling_transmit(hspi2,&t);
}


/******************************************************************************
      函数说明：LCD写入数据
      入口数据：dat 写入的数据
      返回值：  无
******************************************************************************/
void LCD_WR_DATA(uint16_t dat)
{
	LCD_WR_DATA8(dat>>8);
	LCD_WR_DATA8(dat);
}


/******************************************************************************
      函数说明：LCD写入命令
      入口数据：dat 写入的命令
      返回值：  无
******************************************************************************/
void LCD_WR_REG(uint8_t dat)
{
	LCD_DC_Clr();//写命令
	LCD_WR_DATA8(dat);
	LCD_DC_Set();//写数据
}


/******************************************************************************
      函数说明：设置起始和结束地址
      入口数据：x1,x2 设置列的起始和结束地址
                y1,y2 设置行的起始和结束地址
      返回值：  无
******************************************************************************/
void LCD_Address_Set(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2)
{
	if(USE_HORIZONTAL==0)
	{
		LCD_WR_REG(0x2a);//列地址设置
		LCD_WR_DATA(x1);
		LCD_WR_DATA(x2);
		LCD_WR_REG(0x2b);//行地址设置
		LCD_WR_DATA(y1+20);
		LCD_WR_DATA(y2+20);
		//LCD_WR_REG(0x2c);//储存器写
	}
	else if(USE_HORIZONTAL==1)
	{
		LCD_WR_REG(0x2a);//列地址设置
		LCD_WR_DATA(x1);
		LCD_WR_DATA(x2);
		LCD_WR_REG(0x2b);//行地址设置
		LCD_WR_DATA(y1+20);
		LCD_WR_DATA(y2+20);
		LCD_WR_REG(0x2c);//储存器写
	}
	else if(USE_HORIZONTAL==2)
	{
		LCD_WR_REG(0x2a);//列地址设置
		LCD_WR_DATA(x1);
		LCD_WR_DATA(x2);
		LCD_WR_REG(0x2b);//行地址设置
		LCD_WR_DATA(y1);
		LCD_WR_DATA(y2);
		LCD_WR_REG(0x2c);//储存器写
	}
	else
	{
		LCD_WR_REG(0x2a);//列地址设置
		LCD_WR_DATA(x1);
		LCD_WR_DATA(x2);
		LCD_WR_REG(0x2b);//行地址设置
		LCD_WR_DATA(y1);
		LCD_WR_DATA(y2);
		LCD_WR_REG(0x2c);//储存器写
	}
}

void LCD_Init(void)
{

	LCD_RES_Clr();//复位
	delay_ms(100);
	LCD_RES_Set();
	delay_ms(500);

	LCD_BLK_Set();//打开背光
    delay_ms(500);

	//************* Start Initial Sequence **********//
	LCD_WR_REG(0x11); //Sleep out
	delay_ms(120);              //Delay 120ms
	//************* Start Initial Sequence **********//
	LCD_WR_REG(0x36);
	if(USE_HORIZONTAL==0)LCD_WR_DATA8(0x00);
	else if(USE_HORIZONTAL==1)LCD_WR_DATA8(0xC0);
	else if(USE_HORIZONTAL==2)LCD_WR_DATA8(0x70);
	else LCD_WR_DATA8(0xA0);

	LCD_WR_REG(0x3A);
	LCD_WR_DATA8(0x05);

	LCD_WR_REG(0xB2);
	LCD_WR_DATA8(0x0C);
	LCD_WR_DATA8(0x0C);
	LCD_WR_DATA8(0x00);
	LCD_WR_DATA8(0x33);
	LCD_WR_DATA8(0x33);

	LCD_WR_REG(0xB7);
	LCD_WR_DATA8(0x56);

	LCD_WR_REG(0xBB);
	LCD_WR_DATA8(0x20); //Vcom=1.35V?

	LCD_WR_REG(0xC0);
	LCD_WR_DATA8(0x2C);

	LCD_WR_REG(0xC2);
	LCD_WR_DATA8(0x01); //GVDD=4.8V  颜色深度

	LCD_WR_REG(0xC3);
	LCD_WR_DATA8(0x0F); //VDV, 0x20:0v

	LCD_WR_REG(0xC4);
	LCD_WR_DATA8(0x20); //0x0F:60Hz

	LCD_WR_REG(0xC6);
	LCD_WR_DATA8(0x0F); //0x0F:60Hz

	LCD_WR_REG(0xD0);
	LCD_WR_DATA8(0xA4);
	LCD_WR_DATA8(0xA1);

	LCD_WR_REG(0xD6);
	LCD_WR_DATA8(0xA1);

	LCD_WR_REG(0xE0);
	LCD_WR_DATA8(0xf0);
	LCD_WR_DATA8(0x00);
	LCD_WR_DATA8(0x00);
	LCD_WR_DATA8(0x06);
	LCD_WR_DATA8(0x06);
	LCD_WR_DATA8(0x07);
	LCD_WR_DATA8(0x05);
	LCD_WR_DATA8(0x30);
	LCD_WR_DATA8(0x44);
	LCD_WR_DATA8(0x48);
	LCD_WR_DATA8(0x38);
	LCD_WR_DATA8(0x11);
	LCD_WR_DATA8(0x10);
	LCD_WR_DATA8(0x2e);
	LCD_WR_DATA8(0x34);

	LCD_WR_REG(0xE1);
	LCD_WR_DATA8(0xf0);
	LCD_WR_DATA8(0x0a);
	LCD_WR_DATA8(0x0e);
	LCD_WR_DATA8(0x0d);
	LCD_WR_DATA8(0x0b);
	LCD_WR_DATA8(0x27);
	LCD_WR_DATA8(0x2f);
	LCD_WR_DATA8(0x44);
	LCD_WR_DATA8(0x47);
	LCD_WR_DATA8(0x35);
	LCD_WR_DATA8(0x12);
	LCD_WR_DATA8(0x12);
	LCD_WR_DATA8(0x2c);
	LCD_WR_DATA8(0x32);
	LCD_WR_REG(0x35);
	LCD_WR_DATA8(0x00);
	LCD_WR_REG(0x21);
	LCD_WR_REG(0x29);

	LCD_WR_REG(0x2A);
	LCD_WR_DATA8(0x00);
	LCD_WR_DATA8(0x00);
	LCD_WR_DATA8(0x01);
	LCD_WR_DATA8(0x3F);

	LCD_WR_REG(0x2B);
	LCD_WR_DATA8(0x00);
	LCD_WR_DATA8(0x00);
	LCD_WR_DATA8(0x00);
	LCD_WR_DATA8(0xEF);
	LCD_WR_REG(0x2C);
}








