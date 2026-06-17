#include "Uart.h"

static volatile unsigned char rx_buf[UART_RX_BUF_SIZE];
static volatile unsigned char rx_len;
static volatile unsigned long rx_last_tick;

void UART_Init(void)
{
    TMOD &= 0x0F;
    TMOD |= 0x20;

    SCON = 0x50;
    PCON &= 0x7F;

    TH1 = 0xFD;
    TL1 = 0xFD;

    TR1 = 1;

    rx_len = 0;
    rx_last_tick = 0;

    ES = 1;
    EA = 1;
}

void UART_SendByte(unsigned char dat)
{
    ES = 0;
    SBUF = dat;
    while (TI == 0);
    TI = 0;
    ES = 1;
}

void UART_SendString(const char *s)
{
    while (*s)
    {
        UART_SendByte((unsigned char)*s++);
    }
}

void UART_Task(void)
{
    unsigned char len;

    if (rx_len == 0)
        return;

    if ((unsigned long)(uwtick - rx_last_tick) <= UART_RX_TIMEOUT_MS)
        return;

    ES = 0;
    len = rx_len;
    rx_len = 0;
    ES = 1;

    if (len > 0)
    {
        UartApp_OnFrame((const unsigned char*)rx_buf, len);
    }
}

void UART_Routine(void) interrupt 4
{
    unsigned char dat;

    if (RI)
    {
        RI = 0;
        dat = SBUF;

        rx_last_tick = uwtick;

        if (rx_len < UART_RX_BUF_SIZE)
        {
            rx_buf[rx_len++] = dat;
        }
    }

    if (TI)
    {
        TI = 0;
    }
}

