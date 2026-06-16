#include "ble_driver.h"

#include <string.h>
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "host/ble_store.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

void ble_store_config_init(void);

static const char *TAG = "ble_driver";

#define BLE_DEVICE_NAME        "ESP32-SmartLock"
#define BLE_DRIVER_MAX_SUB_CBS 8

static ble_driver_subscribe_cb_t s_sub_cbs[BLE_DRIVER_MAX_SUB_CBS];
static size_t                    s_sub_cb_count = 0;

static ble_driver_conn_cb_t      s_conn_cb = NULL;

static volatile bool     s_is_connected = false;
static volatile uint16_t s_conn_handle  = 0;

static bool s_adv_allowed = false;       /* 默认不广播 */

static void ble_host_task(void *param);
static void on_stack_reset(int reason);
static void on_stack_sync(void);
static int  gap_event_handler(struct ble_gap_event *event, void *arg);

/* ============================================================================
 * BLE 协议栈回调
 * ============================================================================ */

static void on_stack_reset(int reason)
{
    ESP_LOGI(TAG, "BLE stack reset, reason: %d", reason);
}

static void on_stack_sync(void)
{
    int rc;

    ESP_LOGI(TAG, "BLE stack synced");

    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ensure_addr failed: %d", rc);
        return;
    }

    uint8_t addr[6] = {0};
    rc = ble_hs_id_infer_auto(0, &addr[0]);
    if (rc != 0) {
        ESP_LOGE(TAG, "infer_addr failed: %d", rc);
        return;
    }

    ESP_LOGI(TAG, "Device address: %02x:%02x:%02x:%02x:%02x:%02x",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);

    if (s_adv_allowed) {
        ble_driver_start_advertising();
    } else {
        ESP_LOGI(TAG, "Advertising disabled — wait for ENTER_PAIR from 51");
    }
}

static int gap_event_handler(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "Connection %s; status=%d",
                 event->connect.status == 0 ? "established" : "failed",
                 event->connect.status);

        if (event->connect.status == 0) {
            s_conn_handle  = event->connect.conn_handle;
            s_is_connected = true;
            if (s_conn_cb) s_conn_cb(true);
        } else {
            ble_driver_start_advertising();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected; reason=%d", event->disconnect.reason);
        s_is_connected = false;
        s_conn_handle  = 0;
        if (s_conn_cb) s_conn_cb(false);
        /* 不自动重开广播 — 由 on_conn_change (main.c) 决定 */
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "Advertising complete");
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "Subscribe event; attr_handle=%d cur_notify=%d",
                 event->subscribe.attr_handle,
                 event->subscribe.cur_notify);
        for (size_t i = 0; i < s_sub_cb_count; i++) {
            s_sub_cbs[i](event->subscribe.attr_handle,
                         event->subscribe.prev_notify,
                         event->subscribe.cur_notify);
        }
        break;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU update; mtu=%d", event->mtu.value);
        break;

    default:
        break;
    }

    return 0;
}

/* ============================================================================
 * BLE 主机任务
 * ============================================================================ */

static void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* ============================================================================
 * 公共接口
 * ============================================================================ */

esp_err_t ble_driver_init(void)
{
    int rc;

    ESP_LOGI(TAG, "Initializing NimBLE stack");

    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", ret);
        return ESP_FAIL;
    }

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_svc_gap_device_name_set(BLE_DEVICE_NAME);
    if (rc != 0) {
        ESP_LOGE(TAG, "set_device_name failed: %d", rc);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "NimBLE ready; register GATT services now");
    return ESP_OK;
}

esp_err_t ble_driver_start(void)
{
    ESP_LOGI(TAG, "Starting NimBLE host task");

    ble_hs_cfg.reset_cb          = on_stack_reset;
    ble_hs_cfg.sync_cb           = on_stack_sync;
    ble_hs_cfg.gatts_register_cb = NULL;
    ble_hs_cfg.store_status_cb   = ble_store_util_status_rr;

    ble_store_config_init();

    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "NimBLE host task started");
    return ESP_OK;
}

esp_err_t ble_driver_start_advertising(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields  fields;
    int                       rc;

    memset(&fields, 0, sizeof(fields));
    fields.flags            = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name             = (uint8_t *)BLE_DEVICE_NAME;
    fields.name_len         = strlen(BLE_DEVICE_NAME);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_set_fields failed: %d", rc);
        return ESP_FAIL;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min  = 0x20;
    adv_params.itvl_max  = 0x40;

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, gap_event_handler, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_start failed: %d", rc);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Advertising started");
    return ESP_OK;
}

esp_err_t ble_driver_stop_advertising(void)
{
    int rc = ble_gap_adv_stop();
    if (rc != 0) {
        ESP_LOGW(TAG, "adv_stop: %d (not advertising?)", rc);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Advertising stopped");
    return ESP_OK;
}

bool ble_driver_is_connected(void)
{
    return s_is_connected;
}

bool ble_driver_get_conn_handle(uint16_t *out)
{
    if (!s_is_connected) return false;
    if (out) *out = s_conn_handle;
    return true;
}

esp_err_t ble_driver_register_subscribe_cb(ble_driver_subscribe_cb_t cb)
{
    if (cb == NULL) return ESP_ERR_INVALID_ARG;
    if (s_sub_cb_count >= BLE_DRIVER_MAX_SUB_CBS) {
        ESP_LOGE(TAG, "subscribe_cb array full (max=%d)", BLE_DRIVER_MAX_SUB_CBS);
        return ESP_ERR_NO_MEM;
    }
    s_sub_cbs[s_sub_cb_count++] = cb;
    return ESP_OK;
}

esp_err_t ble_driver_register_conn_cb(ble_driver_conn_cb_t cb)
{
    s_conn_cb = cb;
    return ESP_OK;
}

esp_err_t ble_driver_set_adv_allowed(bool allowed)
{
    s_adv_allowed = allowed;
    ESP_LOGI(TAG, "Advertising %s", allowed ? "ENABLED" : "DISABLED");
    return ESP_OK;
}

esp_err_t ble_driver_disconnect(void)
{
    if (!s_is_connected) {
        ESP_LOGW(TAG, "disconnect called but not connected");
        return ESP_ERR_INVALID_STATE;
    }
    int rc = ble_gap_terminate(s_conn_handle, 0x13); /* BLE_ERR_REM_USER_CONN_TERM */
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_terminate failed: %d", rc);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Disconnecting...");
    return ESP_OK;
}

bool ble_driver_is_adv_allowed(void)
{
    return s_adv_allowed;
}
