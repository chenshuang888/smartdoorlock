#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "drivers/ble_driver.h"
#include "services/uart_service.h"
#include "hardware/uart_hw.h"

static const char *TAG = "main";

#define NVS_NS      "ble_lock"
#define NVS_KEY     "auth_key"
#define KEY_LEN     16          /* 128-bit 随机密钥 */
#define HEX_STRLEN  (KEY_LEN * 2)

/* ============================================================================
 * 认证状态机
 * ============================================================================ */

typedef enum {
    AUTH_UNPAIRED,      /* NVS 无密钥，等待首台设备 */
    AUTH_LOCKED,        /* 有密钥，等待 [AUTH] 验证 */
    AUTH_UNLOCKED,      /* 已验证，UART 桥直通 */
} auth_state_t;

static auth_state_t s_state = AUTH_UNPAIRED;
static uint8_t      s_key[KEY_LEN];
static bool         s_has_key = false;
static bool         s_pending_bond = false;   /* 配对中，订阅后发送 [BOND] */
static bool         s_pairing_mode = false;   /* 51 按 # 进入配对模式 */
static bool         s_just_paired = false;    /* 刚完成配对，认证成功时通知 51 */
static bool         s_hello_received = false; /* [HELLO] 是否已到达 */

/* ============================================================================
 * 工具函数
 * ============================================================================ */

static uint8_t hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
    if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
    return 0xFF;
}

static void key_to_hex(char *out)
{
    for (int i = 0; i < KEY_LEN; i++)
        sprintf(out + i * 2, "%02X", s_key[i]);
}

/* ============================================================================
 * NVS 读写密钥
 * ============================================================================ */

static bool load_key(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t sz = KEY_LEN;
    bool ok = (nvs_get_blob(h, NVS_KEY, s_key, &sz) == ESP_OK && sz == KEY_LEN);
    nvs_close(h);
    return ok;
}

static void save_key(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, NVS_KEY, s_key, KEY_LEN);
    nvs_commit(h);
    nvs_close(h);
}

static void erase_key(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "erase: nvs_open failed: %d", err);
        return;
    }
    err = nvs_erase_key(h, NVS_KEY);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "erase: nvs_erase_key failed: %d", err);
    }
    err = nvs_commit(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "erase: nvs_commit failed: %d", err);
    }
    nvs_close(h);
    ESP_LOGI(TAG, "Key erased from NVS");
}

/* ============================================================================
 * 发送 [BOND] 密钥到手机（Notify 订阅后调用）
 * ============================================================================ */

static void send_bond_msg(void)
{
    char hex[HEX_STRLEN + 1];
    key_to_hex(hex);
    char msg[64];
    int n = snprintf(msg, sizeof(msg), "[BOND] %s\n", hex);
    uart_service_send((const uint8_t *)msg, n);
    ESP_LOGI(TAG, "[BOND] sent to phone: hex=%s", hex);
}

/* ============================================================================
 * BLE 连接 / 断开回调
 * ============================================================================ */

static void on_conn_change(bool connected)
{
    if (connected) {
        s_hello_received = false;
        /* 配对模式 → 生成/替换密钥，标记待发送 [BOND] */
        if (s_pairing_mode) {
            s_pairing_mode = false;
            s_just_paired = true;
            esp_fill_random(s_key, KEY_LEN);
            save_key();
            s_has_key = true;
            s_pending_bond = true;
            ESP_LOGI(TAG, "Pairing mode — new key generated, will send [BOND]");
        } else if (!s_has_key) {
            /* 无密钥且非配对模式（不应发生，容错） */
            esp_fill_random(s_key, KEY_LEN);
            save_key();
            s_has_key = true;
            s_pending_bond = true;
            ESP_LOGI(TAG, "New key generated (fallback)");
        }
        s_state = AUTH_LOCKED;
        ESP_LOGI(TAG, "Connected — waiting for auth");
    } else {
        s_pending_bond = false;
        s_pairing_mode = false;
        s_just_paired = false;
        s_hello_received = false;
        if (s_has_key) {
            s_state = AUTH_LOCKED;
            ble_driver_start_advertising();  /* 已配对 — 允许手机重连 */
            ESP_LOGI(TAG, "Disconnected — restart advertising (paired)");
        } else {
            s_state = AUTH_UNPAIRED;
            ble_driver_stop_advertising();
            ble_driver_set_adv_allowed(false);
            ESP_LOGI(TAG, "Disconnected — stay silent, wait for 51");
        }
    }
}

/* ============================================================================
 * Notify 订阅回调 — 此时才可发送 Notify 给手机
 * ============================================================================ */

static void on_subscribe(uint16_t attr_handle, uint8_t prev, uint8_t cur)
{
    (void)attr_handle;
    ESP_LOGI(TAG, "[FW v2] on_subscribe cur=%d prev=%d", cur, prev);
}

/* ============================================================================
 * BLE UART RX 回调 — 认证拦截 + UART 桥
 * ============================================================================ */

static void on_uart_rx(const uint8_t *data, size_t len)
{
    /* ---- 调试消息：手机发 [DBG]xxx 直接打印 ---- */
    if (len >= 5 && memcmp(data, "[DBG]", 5) == 0) {
        ESP_LOGI(TAG, "PHONE: %.*s", (int)(len - 5), data + 5);
        return;
    }

    /* ---- PING 测试：手机端测双向通路 ---- */
    if (len >= 6 && memcmp(data, "[PING]", 6) == 0) {
        ESP_LOGI(TAG, "<<< [PING] from phone >>>");
        uart_service_send((const uint8_t *)"[PONG]\n", 7);
        ESP_LOGI(TAG, ">>> [PONG] sent to phone <<<");
        return;
    }

    /* ---- 所有状态下均先处理 [UNPAIR] ---- */
    if (len >= 8 && memcmp(data, "[UNPAIR]", 8) == 0) {
        erase_key();
        s_has_key = false;
        s_state = AUTH_UNPAIRED;
        ble_driver_set_adv_allowed(false);
        uart_service_send((const uint8_t *)"[UNPAIRED]\n", 11);
        ble_driver_disconnect();
        ESP_LOGI(TAG, "Pairing reset by [UNPAIR]");
        return;
    }

    /* ---- 握手命令 [HELLO]：拉取式配对协议 ---- */
    if (len >= 7 && memcmp(data, "[HELLO]", 7) == 0) {
        s_hello_received = true;
        ESP_LOGI(TAG, "<<< [HELLO] received, pending_bond=%d >>>", s_pending_bond);
        if (s_pending_bond) {
            s_pending_bond = false;
            send_bond_msg();
        } else {
            uart_service_send((const uint8_t *)"[READY]\n", 8);
            ESP_LOGI(TAG, "[READY] sent to phone");
        }
        return;
    }

    /* ---- 已认证：UART 桥直通 → 51 单片机 ---- */
    if (s_state == AUTH_UNLOCKED) {
        ESP_LOGI(TAG, "Forward to 51 (%d bytes): %.*s", len, (int)len, data);
        for (size_t i = 0; i < len; i++)
            UART_HW_SendByte(data[i]);
        return;
    }

    /* ---- 锁定态：仅接受 [AUTH] ---- */
    if (s_state == AUTH_LOCKED) {
        if (len >= 7 + HEX_STRLEN && memcmp(data, "[AUTH] ", 7) == 0) {
            const char *hex = (const char *)data + 7;
            uint8_t got[KEY_LEN];
            bool ok = true;

            ESP_LOGI(TAG, "[AUTH] received (hello=%s)", s_hello_received ? "YES" : "NO");

            for (int i = 0; i < KEY_LEN; i++) {
                uint8_t h = hex_nibble(hex[i * 2]);
                uint8_t l = hex_nibble(hex[i * 2 + 1]);
                if (h == 0xFF || l == 0xFF) { ok = false; break; }
                got[i] = (uint8_t)((h << 4) | l);
            }

            if (ok && memcmp(got, s_key, KEY_LEN) == 0) {
                s_state = AUTH_UNLOCKED;
                uart_service_send((const uint8_t *)"[OK]\n", 5);
                if (s_just_paired) {
                    s_just_paired = false;
                    UART_HW_SendString("[PAIR_OK]\n");
                    ESP_LOGI(TAG, "[PAIR_OK] sent to 51");
                }
                UART_HW_SendString("[UNLOCK]\n");
                ESP_LOGI(TAG, "[UNLOCK] sent to 51 — welcome home");
                ESP_LOGI(TAG, "Auth OK — UART bridge unlocked");
            } else {
                uart_service_send((const uint8_t *)"[FAIL]\n", 7);
                char exp_hex[HEX_STRLEN + 1];
                key_to_hex(exp_hex);
                ESP_LOGW(TAG, "Auth FAILED: got=<%.*s> expected=<%s>", (int)(len - 7), data + 7, exp_hex);
            }
            return;
        }

        /* 锁定期间忽略其他数据 */
        ESP_LOGD(TAG, "Ignored %d bytes while locked", len);
        return;
    }
}

/* ============================================================================
 * 51 单片机串口帧回调
 * ============================================================================ */

void UartHwApp_OnFrame(const uint8_t *data, size_t len)
{
    ESP_LOGI(TAG, "51 RX (%d bytes): %.*s", len, (int)len, data);

    /* 51 按下 # → 擦除旧密钥（如有）→ 开广播等待新手机配对 */
    if (len >= 10 && memcmp(data, "ENTER_PAIR", 10) == 0) {
        if (s_has_key) {
            erase_key();
            s_has_key = false;
            ESP_LOGI(TAG, "Old key erased for re-pairing");
        }
        s_pairing_mode = true;
        ble_driver_set_adv_allowed(true);
        ble_driver_stop_advertising();   /* 停掉再起，避免 BLE_HS_EALREADY */
        ble_driver_start_advertising();
        ESP_LOGI(TAG, "Pairing mode — advertising started");
        return;
    }

    /* 透传 51 回复 → 手机（通过 BLE UART） */
    if (ble_driver_is_connected()) {
        uart_service_send(data, len);
    }
}

/* ============================================================================
 * 入口
 * ============================================================================ */

void app_main(void)
{
    ESP_LOGI(TAG, "Smart Door Lock — BLE UART starting... [FW v2 HELLO-handshake]");

    /* 初始化 NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS full/version mismatch, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 读取已有密钥 */
    s_has_key = load_key();
    if (s_has_key) {
        s_state = AUTH_LOCKED;
        ESP_LOGI(TAG, "Paired — auth required on connect");
    } else {
        s_state = AUTH_UNPAIRED;
        ESP_LOGI(TAG, "Not paired — advertising disabled, wait for 51");
    }

    /* 初始化 BLE + UART 服务 */
    ble_driver_init();

    if (uart_service_init() != ESP_OK) {
        ESP_LOGE(TAG, "UART service init FAILED");
    }

    uart_service_register_rx_cb(on_uart_rx);
    ble_driver_register_conn_cb(on_conn_change);
    ble_driver_register_subscribe_cb(on_subscribe);

    /* 有密钥 → 允许广播（已配对手机可直接连）
     * 无密钥 → 等待 51 发 ENTER_PAIR 才开广播 */
    ble_driver_set_adv_allowed(s_has_key);

    ble_driver_start();

    /* 初始化硬件 UART（与 51 单片机通信） */
    UART_HW_Init();
    xTaskCreate(UART_HW_Task, "uart_hw", 3072, NULL, 10, NULL);
}
