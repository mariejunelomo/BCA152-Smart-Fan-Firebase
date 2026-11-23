
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "secrets.h"

#define LED_LOW_GPIO     GPIO_NUM_4
#define LED_NORMAL_GPIO  GPIO_NUM_5
#define LED_HIGH_GPIO    GPIO_NUM_6

#define WIFI_CONNECTED_BIT BIT0

static const char *TAG = "SMART_FAN";

static EventGroupHandle_t wifi_event_group;

static esp_err_t initialize_leds(void)
{
    gpio_config_t io_config = {
        .pin_bit_mask = (1ULL << LED_LOW_GPIO) |
                        (1ULL << LED_NORMAL_GPIO) |
                        (1ULL << LED_HIGH_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    return gpio_config(&io_config);
}

static void set_fan_power_level(uint8_t power_level)
{
    bool low = false;
    bool normal = false;
    bool high = false;

    switch (power_level) {
        case 0:
            break;

        case 33:
            low = true;
            break;

        case 66:
            normal = true;
            break;

        case 100:
            high = true;
            break;

        default:
            ESP_LOGW(TAG, "Invalid fan power level: %u", power_level);
            return;
    }

    ESP_ERROR_CHECK(gpio_set_level(LED_LOW_GPIO, low));
    ESP_ERROR_CHECK(gpio_set_level(LED_NORMAL_GPIO, normal));
    ESP_ERROR_CHECK(gpio_set_level(LED_HIGH_GPIO, high));

    ESP_LOGI(TAG, "Fan power level: %u%%", power_level);
}

static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Connecting to Wi-Fi...");
        ESP_ERROR_CHECK(esp_wifi_connect());
    }
    else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {

        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);

        ESP_LOGW(TAG, "Wi-Fi disconnected. Reconnecting...");
        ESP_ERROR_CHECK(esp_wifi_connect());
    }
    else if (event_base == IP_EVENT &&
             event_id == IP_EVENT_STA_GOT_IP) {

        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

        ESP_LOGI(TAG, "Wi-Fi connected!");
        ESP_LOGI(TAG, "IP Address: " IPSTR, IP2STR(&event->ip_info.ip));

        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void initialize_wifi(void)
{
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(wifi_event_group != NULL ? ESP_OK : ESP_ERR_NO_MEM);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));

    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &wifi_event_handler,
        NULL
    ));

    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &wifi_event_handler,
        NULL
    ));

    wifi_config_t wifi_config = {0};

    strncpy(
        (char *)wifi_config.sta.ssid,
        WIFI_SSID,
        sizeof(wifi_config.sta.ssid) - 1
    );

    strncpy(
        (char *)wifi_config.sta.password,
        WIFI_PASSWORD,
        sizeof(wifi_config.sta.password) - 1
    );

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(
        WIFI_IF_STA,
        &wifi_config
    ));

    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi station initialized.");
}

void app_main(void)
{
    ESP_LOGI(TAG, "Smart Fan Firebase System Starting...");

    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {

        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(initialize_leds());
    set_fan_power_level(0);

    initialize_wifi();

    ESP_LOGI(TAG, "Smart Fan initialization complete.");
}
