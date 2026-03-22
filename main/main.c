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

// --- BIẾN TOÀN CỤC CHO RAINMAKER ---
esp_rmaker_param_t *temp_param;
esp_rmaker_param_t *hum_param;
esp_rmaker_param_t *gas_param;
esp_rmaker_param_t *air_param;
esp_rmaker_param_t *fire_alert_param;

// =========================================================================
// CALLBACK: XỬ LÝ LỆNH TỪ APP RAINMAKER
// =========================================================================
esp_err_t write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
                   const esp_rmaker_param_val_t val, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    if (ctx) {
        ESP_LOGI(TAG, "Nhan lenh tu App: %s", esp_rmaker_param_get_name(param));
    }
    
    // Nút tắt còi báo động trên App
    // Xử lý Nút nhấn tắt còi
    if (strcmp(esp_rmaker_param_get_name(param), "Mute Buzzer") == 0) {
        bool mute_state = val.val.b;
        if (mute_state) {
            // 1. Tắt còi trên board
            if (xDisplayTask != NULL) {
                xTaskNotify(xDisplayTask, 0, eNoAction);
            }
            
            // 2. Ép App nhả nút về trạng thái tắt (False)
            esp_rmaker_param_val_t reset_val = esp_rmaker_bool(false);
            esp_rmaker_param_update_and_report(param, reset_val);
        }
    }
    return ESP_OK;
}

// =========================================================================
// TASK 1: ĐỌC DỮ LIỆU CẢM BIẾN & ĐẨY LÊN CLOUD
// =========================================================================
void task_sensor_read(void *pvParameters) {
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

            // Gửi dữ liệu lên ESP RainMaker App
            if (temp_param) esp_rmaker_param_update_and_report(temp_param, esp_rmaker_int(data.temperature));
            if (hum_param) esp_rmaker_param_update_and_report(hum_param, esp_rmaker_int(data.humidity));
            if (gas_param) esp_rmaker_param_update_and_report(gas_param, esp_rmaker_int(data.gas_mq2));
            if (air_param) esp_rmaker_param_update_and_report(air_param, esp_rmaker_int(data.air_mq135));
            
            // Đẩy chữ SAFE hoặc DANGER!!! lên điện thoại
            if (fire_alert_param) {
                if (data.fire_danger) {
                    esp_rmaker_param_update_and_report(fire_alert_param, esp_rmaker_str("DANGER!!!"));
                } else {
                    esp_rmaker_param_update_and_report(fire_alert_param, esp_rmaker_str("SAFE"));
                }
            }
            if (air_param) esp_rmaker_param_update_and_report(air_param, esp_rmaker_int(data.air_mq135));
            if (fire_alert_param) esp_rmaker_param_update_and_report(fire_alert_param, esp_rmaker_bool(data.fire_danger));
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

// =========================================================================
// TASK 2: QUẢN LÝ LCD, CÒI & NÚT NHẤN
// =========================================================================
void task_display_alarm(void *pvParameters) {
    char row1[32], row2[32];
    system_data_t local_data;
    bool last_fire_state = false;
    bool is_muted = false;
    uint32_t ulNotificationValue;
    int refresh_counter = 0;

    while (1) {
        // 1. CHỜ NÚT NHẤN (Tối đa 500ms) - Vừa tắt còi, vừa dọn rác LCD
        if (xTaskNotifyWait(0, 0, &ulNotificationValue, pdMS_TO_TICKS(500)) == pdTRUE) {
            ESP_LOGW(TAG, ">>> TAT COI BAO DONG <<<");
            is_muted = true;  
            lcd_clear(); 
            vTaskDelay(pdMS_TO_TICKS(50));
            refresh_counter = 0; 
        }

        // 2. COPY DỮ LIỆU AN TOÀN
        if (xSemaphoreTake(xMutex, portMAX_DELAY)) {
            local_data = data;
            xSemaphoreGive(xMutex);
        }

        // Khôi phục trạng thái còi nếu đã an toàn
        if (!local_data.fire_danger) {
            is_muted = false; 
        }

        // 3. CƠ CHẾ SELF-HEALING
        refresh_counter++;
        if (refresh_counter >= 10 || local_data.fire_danger != last_fire_state) {
            lcd_clear();
            vTaskDelay(pdMS_TO_TICKS(50)); 
            refresh_counter = 0;
            last_fire_state = local_data.fire_danger;
        }

        // 4. HIỂN THỊ CHẬM & ÉP CHUẨN 16 KÝ TỰ
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
    
    // -------------------------------------------------------------------
    // PHẦN 1: KHỞI TẠO HỆ THỐNG CƠ BẢN & RAINMAKER
    // -------------------------------------------------------------------
    
    // Khởi tạo NVS (Bắt buộc)
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Khởi tạo Wi-Fi
    app_network_init();

    // Cấu hình Node RainMaker
    esp_rmaker_config_t rainmaker_cfg = {
        .enable_time_sync = true,
    };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&rainmaker_cfg, "Smart Alarm ESP32", "Air Monitor");

    // Tạo Device
    esp_rmaker_device_t *alarm_device = esp_rmaker_device_create("Monitor Node", NULL, NULL);
    esp_rmaker_device_add_cb(alarm_device, write_cb, NULL);

    // Tạo các Parameters hiển thị (Chỉ đọc)
   temp_param = esp_rmaker_param_create("Temperature", "esp.param.temperature", esp_rmaker_int(0), PROP_FLAG_READ);
    esp_rmaker_param_add_ui_type(temp_param, "esp.ui.text");
    esp_rmaker_device_add_param(alarm_device, temp_param);

    hum_param = esp_rmaker_param_create("Humidity", "esp.param.humidity", esp_rmaker_int(0), PROP_FLAG_READ);
    esp_rmaker_param_add_ui_type(hum_param, "esp.ui.text");
    esp_rmaker_device_add_param(alarm_device, hum_param);

    gas_param = esp_rmaker_param_create("Gas Level", "custom.param.gas", esp_rmaker_int(0), PROP_FLAG_READ);
    esp_rmaker_param_add_ui_type(gas_param, "esp.ui.text");
    // Gas Level - Dùng dạng Thanh trượt (Slider) để hiển thị số phần trăm cho đẹp
    gas_param = esp_rmaker_param_create("Gas Level", "custom.param.gas", esp_rmaker_int(0), PROP_FLAG_READ);
    esp_rmaker_param_add_ui_type(gas_param, "esp.ui.slider");
    esp_rmaker_param_add_bounds(gas_param, esp_rmaker_int(0), esp_rmaker_int(100), esp_rmaker_int(1));
    esp_rmaker_device_add_param(alarm_device, gas_param);

    // Air Quality - Cũng dùng thanh trượt (Slider)
    air_param = esp_rmaker_param_create("Air Quality", "custom.param.air", esp_rmaker_int(0), PROP_FLAG_READ);
    esp_rmaker_param_add_ui_type(air_param, "esp.ui.slider");
    esp_rmaker_param_add_bounds(air_param, esp_rmaker_int(0), esp_rmaker_int(100), esp_rmaker_int(1)); 
    esp_rmaker_device_add_param(alarm_device, air_param);

    // Fire Alert - Hiển thị dạng Text chữ cho trực quan ("SAFE" hoặc "DANGER")
    fire_alert_param = esp_rmaker_param_create("Fire Alert", "custom.param.alert", esp_rmaker_str("SAFE"), PROP_FLAG_READ);
    esp_rmaker_param_add_ui_type(fire_alert_param, "esp.ui.text");
    esp_rmaker_device_add_param(alarm_device, fire_alert_param);

    // Mute Buzzer - Chuyển thành nút nhấn (Push Button)
    esp_rmaker_param_t *mute_param = esp_rmaker_param_create("Mute Buzzer", "custom.param.mute", esp_rmaker_bool(false), PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_param_add_ui_type(mute_param, "esp.ui.toggle");
    esp_rmaker_device_add_param(alarm_device, mute_param); 

   

    // Kích hoạt RainMaker
    esp_rmaker_node_add_device(node, alarm_device);
    esp_rmaker_start();

    // Khởi động Wi-Fi Provisioning
    err = app_network_start(POP_TYPE_RANDOM);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Loi khoi dong Wi-Fi Provisioning");
    }

    // -------------------------------------------------------------------
    // PHẦN 2: KHỞI TẠO PHẦN CỨNG & TASK
    // -------------------------------------------------------------------
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

    // Chạy Task Hiển thị trước để lấy Handle cho nút nhấn
    xTaskCreate(task_display_alarm, "LCD_Alarm_Task", 4096, NULL, 5, &xDisplayTask);
    
    // Khởi tạo nút nhấn phần cứng
    button_init(xDisplayTask);

    // Chạy Task Cảm biến
    xTaskCreate(task_sensor_read, "Sensor_Read_Task", 4096, NULL, 5, NULL);

    vTaskDelay(pdMS_TO_TICKS(5000));
    lcd_clear();
}