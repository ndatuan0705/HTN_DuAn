#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

// --- THƯ VIỆN RAINMAKER & NVS ---
#include <nvs_flash.h>
#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_params.h>
#include <esp_rmaker_standard_devices.h>
#include <app_network.h>

// --- THƯ VIỆN CÁC LINH KIỆN ---
#include "lcd_i2c.h"
#include "dht11.h"
#include "mq2.h"
#include "mq135.h"
#include "buzzer.h"
#include "button.h"

static const char *TAG = "SMART_ALARM";

// --- CẤU HÌNH NGƯỠNG CẢNH BÁO ---
#define TEMP_LIMIT 50 
#define GAS_LIMIT  40 
#define AIR_BAD    60 

typedef struct {
    int temperature;
    int humidity;
    int gas_mq2;
    int air_mq135;
    bool fire_danger;
} system_data_t;

system_data_t data;
SemaphoreHandle_t xMutex;
TaskHandle_t xDisplayTask = NULL;

// --- BIẾN TOÀN CỤC CHO RAINMAKER (Đã thêm buzzer_param ra ngoài) ---
esp_rmaker_param_t *temp_param;
esp_rmaker_param_t *hum_param;
esp_rmaker_param_t *gas_param;
esp_rmaker_param_t *air_param;
esp_rmaker_param_t *fire_alert_param;
esp_rmaker_param_t *buzzer_param; 

// =========================================================================
// CALLBACK: XỬ LÝ LỆNH TỪ APP RAINMAKER
// =========================================================================
esp_err_t write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
                   const esp_rmaker_param_val_t val, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    if (ctx) {
        ESP_LOGI(TAG, "Nhan lenh tu App: %s", esp_rmaker_param_get_name(param));
    }
    
    // Nút trạng thái Còi trên App
    if (strcmp(esp_rmaker_param_get_name(param), "Buzzer State") == 0) {
        bool is_on = val.val.b;
        if (!is_on) { // Nếu người dùng chủ động bấm TẮT trên App
            if (xDisplayTask != NULL) {
                xTaskNotify(xDisplayTask, 0, eNoAction); // Gửi tín hiệu tắt còi
            }
        }
        // Cho phép RainMaker cập nhật trạng thái nút nhấn trên giao diện
        esp_rmaker_param_update_and_report(param, val);
    }
    return ESP_OK;
}

// =========================================================================
// TASK 1: ĐỌC DỮ LIỆU CẢM BIẾN & ĐẨY LÊN CLOUD (CHỐNG SPAM)
// =========================================================================
void task_sensor_read(void *pvParameters) {
    // Biến lưu trạng thái cũ để so sánh (Chỉ gửi khi có sự thay đổi)
    static system_data_t old_data = {-1, -1, -1, -1, false}; 
    
    while (1) {
        if (xSemaphoreTake(xMutex, portMAX_DELAY)) {
            dht11_reading_t dht_res = dht11_read();
            if (dht_res.status == 0) {
                data.temperature = dht_res.temperature;
                data.humidity = dht_res.humidity;
            }
            data.gas_mq2 = mq2_get_percentage();
            data.air_mq135 = mq135_get_air_quality();
            
            // Logic báo cháy
            data.fire_danger = (data.temperature >= TEMP_LIMIT || data.gas_mq2 >= GAS_LIMIT);
            
            xSemaphoreGive(xMutex);

            // --- 1. CHỈ CẬP NHẬT SỐ LIỆU KHI CÓ SỰ THAY ĐỔI (TRÁNH LỖI MQTT BUDGET) ---
            if (data.temperature != old_data.temperature) {
                if (temp_param) esp_rmaker_param_update_and_report(temp_param, esp_rmaker_int(data.temperature));
                old_data.temperature = data.temperature;
            }
            if (data.humidity != old_data.humidity) {
                if (hum_param) esp_rmaker_param_update_and_report(hum_param, esp_rmaker_int(data.humidity));
                old_data.humidity = data.humidity;
            }
            if (data.gas_mq2 != old_data.gas_mq2) {
                if (gas_param) esp_rmaker_param_update_and_report(gas_param, esp_rmaker_int(data.gas_mq2));
                old_data.gas_mq2 = data.gas_mq2;
            }
            if (data.air_mq135 != old_data.air_mq135) {
                if (air_param) esp_rmaker_param_update_and_report(air_param, esp_rmaker_int(data.air_mq135));
                old_data.air_mq135 = data.air_mq135;
            }
            
            // --- 2. ĐỒNG BỘ TRẠNG THÁI CẢNH BÁO & NÚT NHẤN LÊN APP ---
            if (data.fire_danger != old_data.fire_danger) {
                if (data.fire_danger) {
                    if (fire_alert_param) esp_rmaker_param_update_and_report(fire_alert_param, esp_rmaker_str("DANGER!!!"));
                    if (buzzer_param) esp_rmaker_param_update_and_report(buzzer_param, esp_rmaker_bool(true)); // Ép App bật ON
                } else {
                    if (fire_alert_param) esp_rmaker_param_update_and_report(fire_alert_param, esp_rmaker_str("SAFE"));
                    if (buzzer_param) esp_rmaker_param_update_and_report(buzzer_param, esp_rmaker_bool(false)); // Ép App tắt OFF
                }
                old_data.fire_danger = data.fire_danger;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

// =========================================================================
// TASK 2: QUẢN LÝ LCD, CÒI & NÚT NHẤN (Giữ nguyên của bạn)
// =========================================================================
void task_display_alarm(void *pvParameters) {
    char row1[32], row2[32];
    system_data_t local_data;
    bool last_fire_state = false;
    bool is_muted = false;
    uint32_t ulNotificationValue;
    int refresh_counter = 0;

    while (1) {
        // 1. CHỜ NÚT NHẤN 
        if (xTaskNotifyWait(0, 0, &ulNotificationValue, pdMS_TO_TICKS(500)) == pdTRUE) {
            ESP_LOGW(TAG, ">>> TAT COI BAO DONG <<<");
            is_muted = true;  
            lcd_clear(); 
            vTaskDelay(pdMS_TO_TICKS(50)); 
            refresh_counter = 0; 
        }

        if (xSemaphoreTake(xMutex, portMAX_DELAY)) {
            local_data = data;
            xSemaphoreGive(xMutex);
        }

        if (!local_data.fire_danger) {
            is_muted = false; 
        }

        refresh_counter++;
        if (refresh_counter >= 10 || local_data.fire_danger != last_fire_state) {
            lcd_clear();
            vTaskDelay(pdMS_TO_TICKS(50)); 
            refresh_counter = 0;
            last_fire_state = local_data.fire_danger;
        }

        if (local_data.fire_danger) {
            if (!is_muted) buzzer_on();
            else buzzer_off();

            lcd_put_cur(0, 0);
            vTaskDelay(pdMS_TO_TICKS(10)); 
            lcd_send_string("!! FIRE ALARM !!"); 
            vTaskDelay(pdMS_TO_TICKS(20));

            snprintf(row2, sizeof(row2), "T:%02dC GAS:%02d%%   ", local_data.temperature, local_data.gas_mq2);
            lcd_put_cur(1, 0);
            vTaskDelay(pdMS_TO_TICKS(10));
            lcd_send_string(row2);
            vTaskDelay(pdMS_TO_TICKS(20));

        } else {
            buzzer_off();
            
            snprintf(row1, sizeof(row1), "T:%02dC H:%02d%% SAFE", local_data.temperature, local_data.humidity);
            snprintf(row2, sizeof(row2), "G:%02d%% Air:%-4s  ", local_data.gas_mq2, (local_data.air_mq135 > AIR_BAD) ? "POOR" : "GOOD");

            lcd_put_cur(0, 0);
            vTaskDelay(pdMS_TO_TICKS(10));
            lcd_send_string(row1);
            vTaskDelay(pdMS_TO_TICKS(20));
            
            lcd_put_cur(1, 0);
            vTaskDelay(pdMS_TO_TICKS(10));
            lcd_send_string(row2);
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// =========================================================================
// HÀM CHÍNH 
// =========================================================================
void app_main(void) {
    ESP_LOGI(TAG, "===== KHOI DONG HE THONG SMART ALARM =====");
    
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    app_network_init();

    esp_rmaker_config_t rainmaker_cfg = {
        .enable_time_sync = true,
    };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&rainmaker_cfg, "Smart Alarm ESP32", "Air Monitor");

    esp_rmaker_device_t *alarm_device = esp_rmaker_device_create("Monitor Node", NULL, NULL);
    esp_rmaker_device_add_cb(alarm_device, write_cb, NULL);

    temp_param = esp_rmaker_param_create("Temperature", "esp.param.temperature", esp_rmaker_int(0), PROP_FLAG_READ);
    esp_rmaker_param_add_ui_type(temp_param, "esp.ui.text");
    esp_rmaker_device_add_param(alarm_device, temp_param);

    hum_param = esp_rmaker_param_create("Humidity", "esp.param.humidity", esp_rmaker_int(0), PROP_FLAG_READ);
    esp_rmaker_param_add_ui_type(hum_param, "esp.ui.text");
    esp_rmaker_device_add_param(alarm_device, hum_param);

    gas_param = esp_rmaker_param_create("Gas Level", "custom.param.gas", esp_rmaker_int(0), PROP_FLAG_READ);
    esp_rmaker_param_add_ui_type(gas_param, "esp.ui.slider");
    esp_rmaker_param_add_bounds(gas_param, esp_rmaker_int(0), esp_rmaker_int(100), esp_rmaker_int(1));
    esp_rmaker_device_add_param(alarm_device, gas_param);

    air_param = esp_rmaker_param_create("Air Quality", "custom.param.air", esp_rmaker_int(0), PROP_FLAG_READ);
    esp_rmaker_param_add_ui_type(air_param, "esp.ui.slider");
    esp_rmaker_param_add_bounds(air_param, esp_rmaker_int(0), esp_rmaker_int(100), esp_rmaker_int(1)); 
    esp_rmaker_device_add_param(alarm_device, air_param);

    fire_alert_param = esp_rmaker_param_create("Fire Alert", "custom.param.alert", esp_rmaker_str("SAFE"), PROP_FLAG_READ);
    esp_rmaker_param_add_ui_type(fire_alert_param, "esp.ui.text");
    esp_rmaker_device_add_param(alarm_device, fire_alert_param);

    // Sử dụng biến toàn cục buzzer_param đã khai báo ở trên
    buzzer_param = esp_rmaker_param_create("Buzzer State", "custom.param.mute", esp_rmaker_bool(false), PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_param_add_ui_type(buzzer_param, "esp.ui.toggle");
    esp_rmaker_device_add_param(alarm_device, buzzer_param); 

    esp_rmaker_node_add_device(node, alarm_device);
    esp_rmaker_start();

    err = app_network_start(POP_TYPE_RANDOM);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Loi khoi dong Wi-Fi Provisioning");
    }

    xMutex = xSemaphoreCreateMutex();

    lcd_i2c_init();
    lcd_clear();
    vTaskDelay(pdMS_TO_TICKS(100));
    lcd_put_cur(0, 1);
    lcd_send_string("SYSTEM BOOTING");

    mq2_init();    
    vTaskDelay(pdMS_TO_TICKS(100));
    mq135_init();  
    
    dht11_init();
    buzzer_init();

    xTaskCreate(task_display_alarm, "LCD_Alarm_Task", 4096, NULL, 5, &xDisplayTask);
    button_init(xDisplayTask);
    xTaskCreate(task_sensor_read, "Sensor_Read_Task", 4096, NULL, 5, NULL);

    vTaskDelay(pdMS_TO_TICKS(5000));
    lcd_clear();
}