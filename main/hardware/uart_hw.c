#include "uart_hw.h"

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/uart.h"
#include "driver/gpio.h"

#define UART_NUM      UART_NUM_1
#define UART_TXD      GPIO_NUM_17
#define UART_RXD      GPIO_NUM_18
#define UART_BAUD     9600
#define EVT_QUEUE_SZ  20

static const char *TAG = "uart_hw";

static uint8_t  rx_buf[UART_HW_RX_BUF_SIZE];
static size_t   rx_len = 0;
static int64_t  rx_last_tick = 0;
static QueueHandle_t evt_queue = NULL;

/* 用户实现的帧回调 — 收到完整一帧后调用 */
extern void UartHwApp_OnFrame(const uint8_t *data, size_t len);

/* ============================================================================
 * 初始化
 * ============================================================================ */

void UART_HW_Init(void)
{
    uart_config_t cfg = {
        .baud_rate  = UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    uart_param_config(UART_NUM, &cfg);
    uart_set_pin(UART_NUM, UART_TXD, UART_RXD,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    /* 延长硬件 RX 超时（25 字符时间 ≈ 26ms @ 9600），避免切碎短帧 */
    uart_set_rx_timeout(UART_NUM, 25);
    uart_driver_install(UART_NUM, UART_HW_RX_BUF_SIZE * 2,
                        0, EVT_QUEUE_SZ, &evt_queue, 0);

    rx_len = 0;
    rx_last_tick = 0;

    ESP_LOGI(TAG, "UART1  %d baud, TX=%d, RX=%d", UART_BAUD, UART_TXD, UART_RXD);
}

/* ============================================================================
 * 发送（直接发）
 * ============================================================================ */

void UART_HW_SendByte(uint8_t dat)
{
    uart_write_bytes(UART_NUM, (const char *)&dat, 1);
}

void UART_HW_SendString(const char *s)
{
    uart_write_bytes(UART_NUM, s, strlen(s));
}

/* ============================================================================
 * UART 任务（事件队列 + 超时组帧）
 * ============================================================================ */

void UART_HW_Task(void *arg)
{
    ESP_LOGI(TAG, "task started");

    uart_event_t evt;
    while (1) {
        if (xQueueReceive(evt_queue, &evt, pdMS_TO_TICKS(10))) {
            if (evt.type == UART_DATA) {
                uint8_t tmp[UART_HW_RX_BUF_SIZE];
                int len = uart_read_bytes(UART_NUM, tmp, evt.size, 0);
                for (int i = 0; i < len; i++) {
                    if (rx_len < UART_HW_RX_BUF_SIZE)
                        rx_buf[rx_len++] = tmp[i];
                }
                rx_last_tick = esp_timer_get_time() / 1000;
            }
        }

        /* 帧结束：超过 UART_HW_RX_TIMEOUT_MS 没收到新字节 */
        if (rx_len > 0) {
            int64_t now = esp_timer_get_time() / 1000;
            if (now - rx_last_tick >= UART_HW_RX_TIMEOUT_MS) {
                size_t frame_len = rx_len;
                rx_len = 0;
                UartHwApp_OnFrame(rx_buf, frame_len);
            }
        }
    }
}
