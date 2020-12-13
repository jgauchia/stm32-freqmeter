#ifndef LCD_1602_H
#define LCD_1602_H
void delay(int n);
void WriteLcdData(uint8_t method, char ch);
void SetLcdXY(uint8_t x, uint8_t y);
void LCD_Init(void);
void ClearLcdData(void);
void puts_lcd(char *s);
void create_custom_char(uint8_t num, uint8_t* data);
void puts_custom_char(uint8_t num);
#endif

