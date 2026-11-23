#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "SMART_FAN";

void app_main(void)
{
    ESP_LOGI(TAG, "Smart Fan Firebase System Starting...");
    ESP_LOGI(TAG, "Firmware initialized successfully.");
}