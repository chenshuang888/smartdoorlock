#pragma once

#include <stdint.h>
#include <stddef.h>

#define UART_HW_RX_BUF_SIZE     256
#define UART_HW_RX_TIMEOUT_MS   50

void UART_HW_Init(void);
void UART_HW_SendByte(uint8_t dat);
void UART_HW_SendString(const char *s);
void UART_HW_Task(void *arg);
