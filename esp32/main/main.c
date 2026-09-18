#include <inttypes.h>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "robot_ble.h"
#include "robot_ble_protocol_self_test.h"
#include "robot_protocol_self_test.h"
#include "robot_uart_link.h"

static const char *TAG = "robot_boot";

static const char *chip_model_name(esp_chip_model_t model)
{
    switch (model) {
    case CHIP_ESP32:
        return "ESP32";
    case CHIP_ESP32S2:
        return "ESP32-S2";
    case CHIP_ESP32S3:
        return "ESP32-S3";
    case CHIP_ESP32C3:
        return "ESP32-C3";
    case CHIP_ESP32H2:
        return "ESP32-H2";
    case CHIP_ESP32C2:
        return "ESP32-C2";
    case CHIP_ESP32C6:
        return "ESP32-C6";
    default:
        return "UNKNOWN";
    }
}

void app_main(void)
{
    esp_chip_info_t chip_info = {0};
    uint32_t flash_size_bytes = 0;
    uint32_t heartbeat = 0;
    uint32_t protocol_failure_mask = 0;
    uint32_t ble_protocol_failure_mask = 0;
    bool ble_available = false;

    esp_chip_info(&chip_info);

    const esp_err_t flash_result = esp_flash_get_size(NULL, &flash_size_bytes);
    if (flash_result != ESP_OK) {
        ESP_LOGE(TAG, "Unable to read flash size: %s", esp_err_to_name(flash_result));
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Robot ESP32 bootstrap self-test started");
    ESP_LOGI(TAG, "ESP-IDF: %s", esp_get_idf_version());
    ESP_LOGI(TAG,
             "Chip: %s, revision=%u, cores=%u",
             chip_model_name(chip_info.model),
             (unsigned int)chip_info.revision,
             (unsigned int)chip_info.cores);
    ESP_LOGI(TAG,
             "Features: WiFi=%u, BT=%u, BLE=%u",
             (chip_info.features & CHIP_FEATURE_WIFI_BGN) != 0U,
             (chip_info.features & CHIP_FEATURE_BT) != 0U,
             (chip_info.features & CHIP_FEATURE_BLE) != 0U);

    if (flash_result == ESP_OK) {
        ESP_LOGI(TAG, "Flash: %" PRIu32 " MB", flash_size_bytes / (1024U * 1024U));
    }

    ESP_LOGI(TAG, "Reset reason: %d", (int)esp_reset_reason());
    if (!RobotProtocolSelfTest_Run(&protocol_failure_mask)) {
        ESP_LOGE(TAG, "UART V1 protocol self-test: FAIL, mask=0x%08" PRIX32,
                 protocol_failure_mask);
        ESP_LOGE(TAG, "UART2 remains disabled; no command can reach STM32");
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }
    ESP_LOGI(TAG, "Bootstrap self-test: PASS");
    ESP_LOGI(TAG, "UART V1 protocol self-test: PASS");
    ESP_LOGI(TAG, "========================================");

    const esp_err_t link_result = RobotUartLink_Start();
    if (link_result != ESP_OK) {
        ESP_LOGE(TAG, "UART2 link start failed: %s",
                 esp_err_to_name(link_result));
        ESP_LOGE(TAG, "Fail-safe state: UART2 disabled; no motor command sent");
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }

    if (!RobotBleProtocolSelfTest_Run(&ble_protocol_failure_mask)) {
        (void)RobotUartLink_RequestStop();
        ESP_LOGE(TAG, "BLE V1 protocol self-test: FAIL, mask=0x%08" PRIX32,
                 ble_protocol_failure_mask);
        ESP_LOGE(TAG, "BLE disabled; UART safety STOP link remains active");
    } else {
        ESP_LOGI(TAG, "BLE Step 10A protocol self-test: PASS");
        const esp_err_t ble_result = RobotBle_Start();
        if (ble_result != ESP_OK) {
            (void)RobotUartLink_RequestStop();
            ESP_LOGE(TAG, "BLE start failed: %s", esp_err_to_name(ble_result));
            ESP_LOGE(TAG, "BLE disabled; UART safety STOP link remains active");
        } else {
            ble_available = true;
        }
    }

    for (;;) {
        RobotUartLinkDiagnostics_t diagnostics;
        RobotBleDiagnostics_t ble_diagnostics;
        RobotProtocolStatus_t status;
        uint32_t status_age_ms = 0;
        const bool status_valid = RobotUartLink_GetStatus(
            &status, &status_age_ms);

        RobotUartLink_GetDiagnostics(&diagnostics);
        RobotBle_GetDiagnostics(&ble_diagnostics);
        ESP_LOGI(TAG,
                 "heartbeat=%" PRIu32 ", link=%s, status=%s, "
                 "rx=%" PRIu32 ", tx=%" PRIu32 ", uart_errors=%" PRIu32
                 ", free_heap=%" PRIu32,
                 heartbeat++,
                 RobotUartLink_IsOnline() ? "ONLINE" : "OFFLINE",
                 status_valid ? "FRESH" : "UNAVAILABLE",
                 diagnostics.received_frames,
                 diagnostics.transmitted_frames,
                 diagnostics.uart_errors + diagnostics.receive_overflows +
                     diagnostics.transmit_errors,
                 esp_get_free_heap_size());
        ESP_LOGI(TAG,
                 "BLE: ready=%s connected=%s connects=%" PRIu32
                 " disconnects=%" PRIu32 " stops=%" PRIu32
                 " rejected=%" PRIu32 " notify_ok=%" PRIu32
                 " notify_err=%" PRIu32 " adv_retry=%" PRIu32
                 " adv_err=%" PRIu32 " lifecycle_err=%" PRIu32,
                 ble_available && RobotBle_IsReady() ? "YES" : "NO",
                 ble_available && RobotBle_IsConnected() ? "YES" : "NO",
                 ble_diagnostics.connections,
                 ble_diagnostics.disconnections,
                 ble_diagnostics.stop_commands,
                 ble_diagnostics.rejected_commands,
                 ble_diagnostics.notifications,
                 ble_diagnostics.notification_errors,
                 ble_diagnostics.advertising_retries,
                 ble_diagnostics.advertising_errors,
                 ble_diagnostics.lifecycle_errors);

        if (status_valid) {
            ESP_LOGI(TAG,
                     "STM32 status: age=%" PRIu32 "ms battery=%u "
                     "left_pwm=%d right_pwm=%d distance=%u mode=%u "
                     "error=0x%02X valid=0x%02X owner=%u",
                     status_age_ms,
                     (unsigned int)status.battery_mv,
                     (int)status.applied_left_pwm,
                     (int)status.applied_right_pwm,
                     (unsigned int)status.distance_mm,
                     (unsigned int)status.mode,
                     (unsigned int)status.error_status,
                     (unsigned int)status.valid_flags,
                     (unsigned int)status.owner);
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
