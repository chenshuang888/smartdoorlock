#ifndef __OLED_H__
#define __OLED_H__

#include "main.h"

void OLED_Init(void);
void OLED_Clear(void);
void OLED_WriteCommand(unsigned char cmd);
void OLED_WriteData(unsigned char dat);
void OLED_ShowChar(unsigned char x, unsigned char y, unsigned char ch);
void OLED_ShowString(unsigned char x, unsigned char y, unsigned char *str);
void OLED_ShowImage_H16(unsigned char y, unsigned char x,
                        unsigned char width, unsigned char *image);

#endif
