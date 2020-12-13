
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include "lcd1602.h"

/* LCD DAT(D4~D7) PB12~PB15
   LCD E PB10
   LCD RW PB7
   LCD RS PB8
*/
#define LCD_E_LOW gpio_clear(GPIOB, GPIO10)
#define LCD_E_HIGH gpio_set(GPIOB, GPIO10)
#define LCD_RW_LOW gpio_clear(GPIOB, GPIO7)
#define LCD_RW_HIGH gpio_set(GPIOB,GPIO7)
#define LCD_RS_LOW gpio_clear(GPIOB,GPIO8)
#define LCD_RS_HIGH gpio_set(GPIOB,GPIO8)

#define LCD_OUTPUT(x) gpio_set(GPIOB, (x & 0x0f) << 12); gpio_clear(GPIOB, ((~x) & 0x0f) << 12)

void delay(int n)
{
	while(n--)
		__asm__("nop");
}


void puts_lcd(char *s)
{
	while(*s)
	{
		WriteLcdData(0, *s);
		s++;
		delay(10000);
	}
}

void WriteLcdData(uint8_t isInstr, char ch)
{
	if(isInstr)
		LCD_RS_LOW;
	else
		LCD_RS_HIGH;

	LCD_RW_LOW;
	LCD_OUTPUT(ch >> 4);
	delay(10);	
	LCD_E_LOW;
	delay(10);
	LCD_E_HIGH;
	delay(10);
	LCD_OUTPUT(ch);
	delay(10);
	LCD_E_LOW;
	delay(10);
	LCD_E_HIGH;
	delay(10);
}

void SetLcdXY(uint8_t x, uint8_t y)
{
	x &= 0x0f;
	y &= 0x01;

	WriteLcdData(1, 0x80 + y * 0x40 + x);
	delay(200000);
}

void ClearLcdData()
{
	WriteLcdData(1, 0x01);
	delay(100000);
}

void LCD_Init()
{
	LCD_RS_LOW;
	LCD_RW_LOW;
	LCD_E_LOW;
	delay(1000);
	LCD_E_HIGH;
	delay(1000);
	LCD_OUTPUT(0x03);
	delay(1000);
	LCD_E_LOW;
	delay(1000);
	LCD_E_HIGH;
	delay(800000);
	
	LCD_RS_LOW;
	LCD_RW_LOW;
	LCD_E_LOW;
	delay(1000);
	LCD_E_HIGH;
	delay(1000);
	LCD_OUTPUT(0x03);
	delay(1000);
	LCD_E_LOW;
	delay(1000);
	LCD_E_HIGH;
	delay(8000);

	LCD_RS_LOW;
	LCD_RW_LOW;
	LCD_E_LOW;
	delay(1000);
	LCD_E_HIGH;
	delay(1000);
	LCD_OUTPUT(0x03);
	delay(1000);
	LCD_E_LOW;
	delay(1000);
	LCD_E_HIGH;
	delay(8000);
	
	LCD_RS_LOW;
	LCD_RW_LOW;
	LCD_E_LOW;
	delay(1000);
	LCD_E_HIGH;
	delay(1000);
	LCD_OUTPUT(0x02);
	delay(1000);
	LCD_E_LOW;
	delay(1000);
	LCD_E_HIGH;
	delay(8000);

	WriteLcdData(1, 0x2c);
	delay(100000);
	WriteLcdData(1, 0x01);
	delay(100000);
	WriteLcdData(1, 0x0c);
	delay(100000);
	
	
}
