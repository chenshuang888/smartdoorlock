#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 收到 BLE 对端数据的回调
 * @param data 数据指针
 * @param len  数据长度
 */
typedef void (*uart_service_rx_cb_t)(const uint8_t *data, size_t len);

/**
 * @brief 注册 GATT 表（必须在 ble_driver_start 之前调用）
 */
esp_err_t uart_service_init(void);

/**
 * @brief 通过 TX Notify 特征值发送数据给对端
 */
esp_err_t uart_service_send(const uint8_t *data, size_t len);

/**
 * @brief 注册接收回调（RX Write 触发）
 */
esp_err_t uart_service_register_rx_cb(uart_service_rx_cb_t cb);

#ifdef __cplusplus
}
#endif
