#ifndef __UART_H__
#define __UART_H__

#include "main.h"

#define UART_RX_BUF_SIZE     24
#define UART_RX_TIMEOUT_MS   10

void UART_Init(void);
void UART_SendByte(unsigned char dat);
void UART_SendString(const char *s);

void UART_Task(void);

#endif
