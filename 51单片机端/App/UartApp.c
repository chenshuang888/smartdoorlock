#include "UartApp.h"
#include "MatrixKeyApp.h"

static void send_ok(void)
{
    UART_SendString("[PWD_OK]\n");
}

static void send_err(void)
{
    UART_SendString("[PWD_ERR]\n");
}

void UartApp_OnFrame(const unsigned char *buf, unsigned char len)
{
    unsigned char i;
    unsigned char tmp[6];

    // =========================================
    // [PWD_QUERY] — 查询当前密码
    // =========================================
    if (len >= 10 && buf[0] == '[' && buf[1] == 'P'
        && buf[2] == 'W' && buf[3] == 'D'
        && buf[4] == '_' && buf[5] == 'Q'
        && buf[6] == 'U' && buf[7] == 'E'
        && buf[8] == 'R' && buf[9] == 'Y')
    {
        MatrixKeyApp_GetPassword(tmp);
        UART_SendString("[PWD_DATA:");
        for (i = 0; i < 6; i++)
            UART_SendByte(tmp[i] + '0');
        UART_SendString("]\n");
        return;
    }

    // =========================================
    // [PWD_SET:xxxxxx] — 修改密码 (6 位数字)
    // =========================================
    // 最小长度: [PWD_SET:123456] = 16 字节
    if (len >= 16 && buf[0] == '[' && buf[1] == 'P'
        && buf[2] == 'W' && buf[3] == 'D'
        && buf[4] == '_' && buf[5] == 'S'
        && buf[6] == 'E' && buf[7] == 'T'
        && buf[8] == ':')
    {
        // 提取 6 位数字
        for (i = 0; i < 6; i++)
        {
            unsigned char c = buf[9 + i];
            if (c < '0' || c > '9')
            {
                send_err();
                return;
            }
            tmp[i] = c - '0';
        }

        MatrixKeyApp_SetPassword(tmp);
        send_ok();
        return;
    }

    // =========================================
    // [PAIR_OK] — ESP32 通知配对成功
    // =========================================
    if (len >= 8 && buf[0] == '[' && buf[1] == 'P'
        && buf[2] == 'A' && buf[3] == 'I'
        && buf[4] == 'R' && buf[5] == '_'
        && buf[6] == 'O' && buf[7] == 'K')
    {
        MatrixKeyApp_OnPairOk();
        return;
    }

    // =========================================
    // [UNLOCK] — 手机认证成功，进入欢迎回家
    // =========================================
    if (len >= 7 && buf[0] == '[' && buf[1] == 'U'
        && buf[2] == 'N' && buf[3] == 'L'
        && buf[4] == 'O' && buf[5] == 'C'
        && buf[6] == 'K')
    {
        MatrixKeyApp_OnUnlock();
        return;
    }

    // =========================================
    // 未知指令 — 回显收到的数据（调试用）
    // =========================================
    UART_SendString("[ECHO:");
    for (i = 0; i < len && i < 16; i++)
    {
        if (buf[i] >= ' ' && buf[i] <= '~')
            UART_SendByte(buf[i]);
        else
        {
            UART_SendByte('\\');
            UART_SendByte('x');
            UART_SendByte("0123456789ABCDEF"[buf[i] >> 4]);
            UART_SendByte("0123456789ABCDEF"[buf[i] & 0x0F]);
        }
    }
    UART_SendString("]\n");
}
