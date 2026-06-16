#include "uart_service.h"
#include "ble_driver.h"

#include <string.h>
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_gatt.h"

static const char *TAG = "uart_svc";

/* ============================================================================
 * NUS (Nordic UART Service) UUIDs
 * Service: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
 *   RX (Write): 6E400002-B5A3-F393-E0A9-E50E24DCCA9E
 *   TX (Notify): 6E400003-B5A3-F393-E0A9-E50E24DCCA9E
 * ============================================================================ */

static const ble_uuid128_t s_svc_uuid = BLE_UUID128_INIT(
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E
);

static const ble_uuid128_t s_rx_uuid = BLE_UUID128_INIT(
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E
);

static const ble_uuid128_t s_tx_uuid = BLE_UUID128_INIT(
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E
);

static uint16_t s_rx_val_handle;
static uint16_t s_tx_val_handle;
static uart_service_rx_cb_t s_rx_cb = NULL;

/* ============================================================================
 * RX 特征值：收到对端写入数据
 * ============================================================================ */

static int rx_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len == 0) return 0;

    uint8_t buf[len];
    int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, len, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "mbuf flatten failed: %d", rc);
        return BLE_ATT_ERR_UNLIKELY;
    }

    if (s_rx_cb) {
        s_rx_cb(buf, len);
    }

    ESP_LOGI("uart_raw", "RX(%d): %.*s", len, (int)len, buf);

    return 0;
}

/* ============================================================================
 * TX 读回调（NOTIFY 特征值需要 read handler）
 * ============================================================================ */

static int tx_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)ctxt;
    (void)arg;
    /* 返回空数据即可 */
    return 0;
}

/* ============================================================================
 * GATT 表：Service → RX (Write) → TX (Notify)
 * ============================================================================ */

static const struct ble_gatt_chr_def s_uart_chrs[] = {
    {
        .uuid       = &s_rx_uuid.u,
        .access_cb  = rx_access_cb,
        .flags      = BLE_GATT_CHR_F_WRITE,
        .val_handle = &s_rx_val_handle,
    },
    {
        .uuid       = &s_tx_uuid.u,
        .access_cb  = tx_access_cb,
        .flags      = BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &s_tx_val_handle,
    },
    { 0 }
};

static const struct ble_gatt_svc_def s_uart_svcs[] = {
    {
        .type            = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid            = &s_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def *)s_uart_chrs,
    },
    { 0 }
};

/* ============================================================================
 * 公共接口
 * ============================================================================ */

esp_err_t uart_service_init(void)
{
    int rc;

    rc = ble_gatts_count_cfg(s_uart_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "count_cfg failed: %d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(s_uart_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "add_svcs failed: %d", rc);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "UART service registered (NUS)");
    return ESP_OK;
}

esp_err_t uart_service_send(const uint8_t *data, size_t len)
{
    uint16_t conn_handle;
    if (!ble_driver_get_conn_handle(&conn_handle)) {
        return ESP_ERR_INVALID_STATE;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (!om) {
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gatts_notify_custom(conn_handle, s_tx_val_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "notify failed: %d", rc);
        return ESP_FAIL;
    }

    ESP_LOGI("uart_raw", "TX(%d): %.*s", len, (int)len, data);
    return ESP_OK;
}

esp_err_t uart_service_register_rx_cb(uart_service_rx_cb_t cb)
{
    if (cb == NULL) return ESP_ERR_INVALID_ARG;
    s_rx_cb = cb;
    return ESP_OK;
}
