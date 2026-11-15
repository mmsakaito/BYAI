#include <stdbool.h>

#include <string.h>

#include "esp_err.h"
#include "sdkconfig.h"

#include "adf_event_hub.h"
#include "esp_config_storage.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_service.h"
#include "esp_wifi.h"
#include "esp_wifi_service.h"
#include "esp_wifi_service_profile_mgr.h"
#include "nvs_flash.h"

static const char *TAG = "byai_network";

static const esp_config_storage_nvs_t s_nvs_context = {
    .nvs_namespace = "byai_wifi",
    .key_primary = "prof_p",
    .key_backup = "prof_b",
};

static struct {
    esp_config_storage_t storage;
    esp_wifi_service_profile_mgr_t manager;
    esp_wifi_service_t *service;
    void (*on_ready)(bool ready);
    bool subscribed;
    bool started;
} s_network;

/** @brief 只在事件有效时复制 IP 数据；不保留发布者借用的 payload。 */
static void network_event_handler(const adf_event_t *event, void *ctx)
{
    (void)ctx;
    if (event == NULL) {
        return;
    }

    bool ready;
    switch (event->event_id) {
    case ESP_WIFI_SERVICE_EVENT_STA_GOT_IP: {
        if (event->payload == NULL || event->payload_len != sizeof(ip_event_got_ip_t)) {
            return;
        }
        ip_event_got_ip_t got_ip;
        memcpy(&got_ip, event->payload, sizeof(got_ip));
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&got_ip.ip_info.ip));
        ready = true;
        break;
    }
    case ESP_WIFI_SERVICE_EVENT_DISCONNECTED:
        if (event->payload == NULL ||
            event->payload_len != sizeof(wifi_event_sta_disconnected_t)) {
            return;
        }
        ready = false;
        break;
    case ESP_WIFI_SERVICE_EVENT_STA_LOST_IP:
        if (event->payload_len != 0) {
            return;
        }
        ready = false;
        break;
    default:
        return;
    }

    if (s_network.on_ready != NULL) {
        s_network.on_ready(ready);
    }
}

/** @brief 使用本地目标网络；只更新对应 SSID，保留其他已存 profile。 */
static esp_err_t connect_if_unconfigured(void)
{
    const char *configured_ssid = CONFIG_BYAI_WIFI_SSID;
    if (configured_ssid[0] != '\0') {
        const char *configured_password = CONFIG_BYAI_WIFI_PASSWORD;
        char ssid[ESP_WIFI_SERVICE_PROFILE_SSID_MAX_LEN + 1];
        char password[ESP_WIFI_SERVICE_PROFILE_PASS_MAX_LEN + 1];
        const size_t ssid_len = strlen(configured_ssid);
        const size_t password_len = strlen(configured_password);
        if (ssid_len > ESP_WIFI_SERVICE_PROFILE_SSID_MAX_LEN ||
            password_len > ESP_WIFI_SERVICE_PROFILE_PASS_MAX_LEN) {
            return ESP_ERR_INVALID_ARG;
        }

        memcpy(ssid, configured_ssid, ssid_len + 1);
        memcpy(password, configured_password, password_len + 1);
        esp_wifi_service_profile_t saved_profile = {0};
        esp_err_t err = esp_wifi_service_profile_mgr_get(s_network.manager, ssid, &saved_profile);
        uint8_t priority = 0;
        bool request_connection = true;
        if (err == ESP_OK) {
            priority = saved_profile.priority;
            const bool password_matches = strcmp(saved_profile.password, password) == 0;
            char last_working[ESP_WIFI_SERVICE_PROFILE_SSID_MAX_LEN + 1] = {0};
            esp_err_t last_err = esp_wifi_service_profile_mgr_get_last_working(
                s_network.manager, last_working, sizeof(last_working));
            if (last_err != ESP_OK && last_err != ESP_ERR_NOT_FOUND) {
                err = last_err;
            } else {
                request_connection = !password_matches || last_err != ESP_OK ||
                                     strcmp(last_working, ssid) != 0;
            }
        } else if (err == ESP_ERR_NOT_FOUND) {
            err = ESP_OK;
        }

        if (err == ESP_OK && request_connection) {
            err = esp_wifi_service_request_connect(s_network.service, ssid, password, priority, 0);
        }

        volatile unsigned char *password_wipe = (volatile unsigned char *)password;
        for (size_t i = 0; i < sizeof(password); ++i) {
            password_wipe[i] = 0;
        }
        volatile unsigned char *saved_password_wipe = (volatile unsigned char *)saved_profile.password;
        for (size_t i = 0; i < sizeof(saved_profile.password); ++i) {
            saved_password_wipe[i] = 0;
        }
        return err;
    }

    uint8_t count = 0;
    esp_err_t err = esp_wifi_service_profile_mgr_count(s_network.manager, &count);
    if (err != ESP_OK) {
        return err;
    }
    if (count == 0) {
        ESP_LOGI(TAG, "Wi-Fi unconfigured");
    }
    return ESP_OK;
}

/**
 * @brief 逆序释放资源，不改写调用者保存的首个错误。
 *
 * 服务销毁失败时保留 service、manager 和 storage，防止服务继续引用已释放依赖。
 * NVS 为进程级共享资源，不由本模块反初始化。
 */
static void network_cleanup(void)
{
    if (s_network.service != NULL) {
        if (s_network.subscribed) {
            esp_err_t err = esp_service_event_unsubscribe(
                (esp_service_t *)s_network.service, NULL, ADF_EVENT_ANY_ID);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Unsubscribe failed: %s", esp_err_to_name(err));
            } else {
                s_network.subscribed = false;
            }
        }
        esp_err_t err = esp_wifi_service_destroy(s_network.service);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Service destroy failed: %s", esp_err_to_name(err));
            return;
        }
        s_network.service = NULL;
        s_network.subscribed = false;
    }
    if (s_network.manager != NULL) {
        esp_wifi_service_profile_mgr_deinit(s_network.manager);
        s_network.manager = NULL;
    }
    if (s_network.storage != NULL) {
        esp_config_storage_deinit(s_network.storage);
        s_network.storage = NULL;
    }
    s_network.on_ready = NULL;
}

esp_err_t byai_network_start(void (*on_ready)(bool ready))
{
    if (s_network.started || s_network.service != NULL || s_network.manager != NULL ||
        s_network.storage != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        return err;
    }
    s_network.on_ready = on_ready;
    err = esp_config_storage_init_nvs(&s_nvs_context, &s_network.storage);
    if (err != ESP_OK) {
        goto fail;
    }
    const esp_wifi_service_profile_mgr_cfg_t manager_config = {
        .max_profiles = 8,
        .storage = s_network.storage,
        .crypto = NULL,
        .crypto_extra_size = 0,
    };
    err = esp_wifi_service_profile_mgr_init(&manager_config, &s_network.manager);
    if (err != ESP_OK) {
        goto fail;
    }
    const esp_wifi_service_config_t service_config = {
        .name = "wifi_service",
        .profile_manager = s_network.manager,
        .prov_list = NULL,
        .prov_num = 0,
        .selector_policy = NULL,
    };
    err = esp_wifi_service_create(&service_config, &s_network.service);
    if (err != ESP_OK) {
        goto fail;
    }
    adf_event_subscribe_info_t subscription = ADF_EVENT_SUBSCRIBE_INFO_DEFAULT();
    subscription.handler = network_event_handler;
    err = esp_service_event_subscribe((esp_service_t *)s_network.service, &subscription);
    if (err != ESP_OK) {
        goto fail;
    }
    s_network.subscribed = true;
    err = esp_service_start((esp_service_t *)s_network.service);
    if (err != ESP_OK) {
        goto fail;
    }
    err = connect_if_unconfigured();
    if (err != ESP_OK) {
        goto fail;
    }
    s_network.started = true;
    return ESP_OK;

fail:
    network_cleanup();
    return err;
}
