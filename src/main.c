
#include <stdint.h>
#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_log.h"

#define LED_LOW_GPIO     GPIO_NUM_4
#define LED_NORMAL_GPIO  GPIO_NUM_5
#define LED_HIGH_GPIO    GPIO_NUM_6

static const char *TAG = "SMART_FAN";

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

void app_main(void)
{
    ESP_LOGI(TAG, "Smart Fan Firebase System Starting...");

    ESP_ERROR_CHECK(initialize_leds());

    set_fan_power_level(0);

    ESP_LOGI(TAG, "LED controller initialized successfully.");
}
