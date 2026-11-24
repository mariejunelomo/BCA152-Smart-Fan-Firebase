#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

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

#define HTTP_RESPONSE_MAX  4096
#define ID_TOKEN_MAX       1536
#define REFRESH_TOKEN_MAX  1024
#define FIREBASE_URL_MAX   1800
#define POLL_INTERVAL_MS   2000
#define SIGN_IN_RETRY_MS   10000

static char http_response[HTTP_RESPONSE_MAX];
static int http_response_len = 0;

static char firebase_id_token[ID_TOKEN_MAX];
static char firebase_refresh_token[REFRESH_TOKEN_MAX];
static int firebase_token_lifetime_s = 0;

static char firebase_url[FIREBASE_URL_MAX];

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (http_response_len + evt->data_len < HTTP_RESPONSE_MAX - 1) {
            memcpy(http_response + http_response_len, evt->data, evt->data_len);
            http_response_len += evt->data_len;
            http_response[http_response_len] = '\0';
        } else {
            ESP_LOGW(TAG, "HTTP response too large");
        }
    }
    return ESP_OK;
}

static esp_err_t firebase_sign_in(void)
{
    esp_err_t result = ESP_FAIL;

    http_response_len = 0;
    http_response[0] = '\0';

    cJSON *body = cJSON_CreateObject();
    if (body == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(body, "email", FIREBASE_USER_EMAIL);
    cJSON_AddStringToObject(body, "password", FIREBASE_USER_PASSWORD);
    cJSON_AddBoolToObject(body, "returnSecureToken", true);

    char *body_text = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (body_text == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t config = {
        .url = "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=" FIREBASE_API_KEY,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        free(body_text);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body_text, strlen(body_text));

    esp_err_t err = esp_http_client_perform(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTPS request failed: %s", esp_err_to_name(err));
    } else {
        int status = esp_http_client_get_status_code(client);

        if (status != 200) {
            ESP_LOGE(TAG, "Firebase sign-in HTTP status: %d", status);
            ESP_LOGE(TAG, "Firebase reply: %s", http_response);
        } else {
            cJSON *root = cJSON_Parse(http_response);
            if (root == NULL) {
                ESP_LOGE(TAG, "Could not parse Firebase reply.");
            } else {
                cJSON *id = cJSON_GetObjectItem(root, "idToken");
                cJSON *refresh = cJSON_GetObjectItem(root, "refreshToken");
                cJSON *expires = cJSON_GetObjectItem(root, "expiresIn");

                if (cJSON_IsString(id) && cJSON_IsString(refresh) &&
                    cJSON_IsString(expires) &&
                    strlen(id->valuestring) < ID_TOKEN_MAX &&
                    strlen(refresh->valuestring) < REFRESH_TOKEN_MAX) {

                    snprintf(firebase_id_token, sizeof(firebase_id_token),
                             "%s", id->valuestring);
                    snprintf(firebase_refresh_token, sizeof(firebase_refresh_token),
                             "%s", refresh->valuestring);
                    firebase_token_lifetime_s = atoi(expires->valuestring);
                    result = ESP_OK;
                } else {
                    ESP_LOGE(TAG, "Firebase reply is missing expected fields.");
                }
                cJSON_Delete(root);
            }
        }
    }

    esp_http_client_cleanup(client);
    free(body_text);
    return result;
}

/* Reads /fan/powerlevel from the Realtime Database (REST API).
   The URL contains the token, so it is never printed. */
static esp_err_t firebase_read_power_level(int *level_out)
{
    esp_err_t result = ESP_FAIL;

    http_response_len = 0;
    http_response[0] = '\0';

    size_t base_len = strlen(FIREBASE_DATABASE_URL);
    while (base_len > 0 && FIREBASE_DATABASE_URL[base_len - 1] == '/') {
        base_len--;
    }

    int n = snprintf(firebase_url, sizeof(firebase_url),
                     "%.*s/fan/powerlevel.json?auth=%s",
                     (int)base_len, FIREBASE_DATABASE_URL,
                     firebase_id_token);
    if (n < 0 || n >= (int)sizeof(firebase_url)) {
        ESP_LOGE(TAG, "Database URL is too long.");
        return ESP_ERR_INVALID_SIZE;
    }

    esp_http_client_config_t config = {
        .url = firebase_url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Database request failed: %s", esp_err_to_name(err));
    } else {
        int status = esp_http_client_get_status_code(client);

        if (status != 200) {
            ESP_LOGE(TAG, "Database HTTP status: %d", status);
            ESP_LOGE(TAG, "Database reply: %s", http_response);
        } else {
            char *end = NULL;
            long value = strtol(http_response, &end, 10);

            if (end == http_response) {
                ESP_LOGW(TAG, "Database value is not a number: %s", http_response);
            } else {
                *level_out = (int)value;
                result = ESP_OK;
            }
        }
    }

    esp_http_client_cleanup(client);
    return result;
}

static void firebase_task(void *pvParameters)
{
    xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        portMAX_DELAY
    );

    while (true) {
        ESP_LOGI(TAG, "Signing in to Firebase...");

        if (firebase_sign_in() == ESP_OK) {
            ESP_LOGI(TAG, "Firebase sign-in OK. Token valid for %d seconds.",
                     firebase_token_lifetime_s);
            break;
        }

        ESP_LOGE(TAG, "Firebase sign-in failed. Retrying in %d seconds.",
                 SIGN_IN_RETRY_MS / 1000);
        vTaskDelay(pdMS_TO_TICKS(SIGN_IN_RETRY_MS));
    }

    int last_level = -1;

    while (true) {
        xEventGroupWaitBits(
            wifi_event_group,
            WIFI_CONNECTED_BIT,
            pdFALSE,
            pdTRUE,
            portMAX_DELAY
        );

        int level = 0;

        if (firebase_read_power_level(&level) == ESP_OK) {
            if (level != last_level) {
                if (level == 0 || level == 33 || level == 66 || level == 100) {
                    set_fan_power_level((uint8_t)level);
                } else {
                    ESP_LOGW(TAG, "Ignoring invalid value from Firebase: %d", level);
                }
                last_level = level;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
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

    xTaskCreate(firebase_task, "firebase_task", 8192, NULL, 5, NULL);

    ESP_LOGI(TAG, "Smart Fan initialization complete.");
}