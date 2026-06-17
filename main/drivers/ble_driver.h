#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ble_driver_subscribe_cb_t)(uint16_t attr_handle,
                                          uint8_t prev_notify,
                                          uint8_t cur_notify);
typedef void (*ble_driver_conn_cb_t)(bool connected);

esp_err_t ble_driver_init(void);
esp_err_t ble_driver_start(void);
esp_err_t ble_driver_start_advertising(void);
esp_err_t ble_driver_stop_advertising(void);
bool ble_driver_is_connected(void);
bool ble_driver_get_conn_handle(uint16_t *out);
esp_err_t ble_driver_register_subscribe_cb(ble_driver_subscribe_cb_t cb);
esp_err_t ble_driver_register_conn_cb(ble_driver_conn_cb_t cb);
esp_err_t ble_driver_set_adv_allowed(bool allowed);
bool ble_driver_is_adv_allowed(void);
esp_err_t ble_driver_disconnect(void);

#ifdef __cplusplus
}
#endif
