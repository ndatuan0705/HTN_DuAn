#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

// --- THƯ VIỆN RAINMAKER & NETWORK ---
#include <nvs_flash.h>
#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_params.h>
#include <esp_rmaker_standard_devices.h>
#include <app_network.h>

// --- THƯ VIỆN CÁC NGOẠI VI TỰ VIẾT ---
#include "lcd_i2c.h"
#include "dht11.h"
#include "mq_sensors.h" // Thư viện cảm biến MQ đã được gộp gọn gàng
#include "buzzer.h"
#include "button.h"

static const char *TAG = "SMART_ALARM";

// --- CẤU HÌNH NGƯỠNG CẢNH BÁO ---
#define TEMP_LIMIT 50 
#define GAS_LIMIT  40 
#define AIR_BAD    60 

// Cấu trúc gói gọn dữ liệu của hệ thống
typedef struct {
    int temperature;
    int humidity;
    int gas_mq2;
    int air_mq135;
    bool fire_danger;
} system_data_t;

system_data_t data;

// Khai báo các công cụ của FreeRTOS
SemaphoreHandle_t xMutex;         // Chìa khóa bảo vệ dữ liệu dùng chung
TaskHandle_t xDisplayTask = NULL; // Thẻ quản lý Task Màn hình & Còi

// --- ĐIỂM KẾT NỐI VỚI CLOUD RAINMAKER ---
esp_rmaker_param_t *temp_param, *hum_param, *gas_param, *air_param, *fire_alert_param, *buzzer_param; 

// =========================================================================
// CALLBACK: LẮNG NGHE LỆNH TỪ APP RAINMAKER
// =========================================================================
esp_err_t write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
                   const esp_rmaker_param_val_t val, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    if (ctx) {
        ESP_LOGI(TAG, "Nhan lenh tu App: %s", esp_rmaker_param_get_name(param));
    }
    
    // Nếu người dùng thao tác trên nút "Buzzer State" ở điện thoại
    if (strcmp(esp_rmaker_param_get_name(param), "Buzzer State") == 0) {
        bool is_on = val.val.b;
        
        // Nếu người dùng bấm TẮT trên App -> Gửi tín hiệu đánh thức Task Màn hình để tắt còi
        if (!is_on && xDisplayTask != NULL) {
            xTaskNotify(xDisplayTask, 0, eNoAction); 
        }
        // Phản hồi lại cho RainMaker biết là lệnh đã được xử lý
        esp_rmaker_param_update_and_report(param, val);
    }
    return ESP_OK;
}

// =========================================================================
// TASK 1: ĐỌC CẢM BIẾN & ĐẨY LÊN CLOUD (CHỐNG SPAM)
// =========================================================================
void task_sensor_read(void *pvParameters) {
    // Biến lưu trạng thái cũ để so sánh (Khởi tạo giá trị âm để chắc chắn lần đầu sẽ cập nhật lên Cloud)
    static system_data_t old_data = {-1, -1, -1, -1, false}; 
    
    while (1) {
        // Xin chìa khóa Mutex để an toàn ghi đè dữ liệu
        if (xSemaphoreTake(xMutex, portMAX_DELAY)) {
            
            dht11_reading_t dht_res = dht11_read();
            if (dht_res.status == 0) {
                data.temperature = dht_res.temperature;
                data.humidity = dht_res.humidity;
            }
            data.gas_mq2 = mq2_get_gas_percentage();
            data.air_mq135 = mq135_get_air_quality();
            
            // Đánh giá nguy cơ cháy nổ
            data.fire_danger = (data.temperature >= TEMP_LIMIT || data.gas_mq2 >= GAS_LIMIT);
            
            // Xử lý xong thì trả chìa khóa Mutex
            xSemaphoreGive(xMutex);

            // --- KIỂM TRA SỰ THAY ĐỔI & CẬP NHẬT LÊN CLOUD ---
            // Việc này giúp tránh bị giới hạn (Rate Limit/Budget) từ server MQTT của AWS
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
            
            // --- ĐỒNG BỘ TRẠNG THÁI CẢNH BÁO ---
            if (data.fire_danger != old_data.fire_danger) {
                if (data.fire_danger) {
                    if (fire_alert_param) esp_rmaker_param_update_and_report(fire_alert_param, esp_rmaker_str("DANGER!!!"));
                    if (buzzer_param) esp_rmaker_param_update_and_report(buzzer_param, esp_rmaker_bool(true)); // Bật còi trên App
                } else {
                    if (fire_alert_param) esp_rmaker_param_update_and_report(fire_alert_param, esp_rmaker_str("SAFE"));
                    if (buzzer_param) esp_rmaker_param_update_and_report(buzzer_param, esp_rmaker_bool(false)); // Tắt còi trên App
                }
                old_data.fire_danger = data.fire_danger;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(3000)); // Nghỉ 3 giây rồi đọc tiếp
    }
}

// =========================================================================
// TASK 2: QUẢN LÝ MÀN HÌNH LCD, CÒI BÁO & NÚT NHẤN (CHỐNG NHẤY MÀN HÌNH)
// =========================================================================
void task_display_alarm(void *pvParameters) {
    char row1[17], row2[17]; // Buffer 16 ký tự + 1 ký tự null
    system_data_t local_data;
    bool is_muted = false;
    uint32_t ulNotificationValue;

    while (1) {
        // 1. Lắng nghe tín hiệu ngắt từ nút bấm cơ hoặc từ App (thời gian chờ tối đa 200ms)
        // Nếu không có ai bấm, lệnh sẽ timeout sau 200ms và chạy tiếp code bên dưới.
        if (xTaskNotifyWait(0, 0, &ulNotificationValue, pdMS_TO_TICKS(200)) == pdTRUE) {
            ESP_LOGW(TAG, "TIN HIEU: TAT COI BAO DONG!");
            is_muted = true;
            // Ép đồng bộ trạng thái tắt còi ngược lại lên App điện thoại
            if (buzzer_param) esp_rmaker_param_update_and_report(buzzer_param, esp_rmaker_bool(false));
        }

        // 2. Xin chìa khóa Mutex để copy dữ liệu an toàn ra biến cục bộ
        if (xSemaphoreTake(xMutex, portMAX_DELAY)) {
            local_data = data;
            xSemaphoreGive(xMutex);
        }

        // 3. Tự động reset trạng thái "Tắt âm" nếu hết nguy hiểm
        if (!local_data.fire_danger) {
            is_muted = false; 
        }

        // 4. Điều khiển Còi
        if (local_data.fire_danger && !is_muted) buzzer_on();
        else buzzer_off();

        // 5. Cập nhật giao diện LCD (Định dạng đủ 16 ký tự để ghi đè, tránh nháy màn hình)
        if (local_data.fire_danger) {
            snprintf(row1, sizeof(row1), "%-16s", "!! FIRE ALARM !!");
            snprintf(row2, sizeof(row2), "T:%02dC GAS:%02d%%   ", local_data.temperature, local_data.gas_mq2);
        } else {
            snprintf(row1, sizeof(row1), "T:%02dC H:%02d%% SAFE", local_data.temperature, local_data.humidity);
            snprintf(row2, sizeof(row2), "G:%02d%% Air:%-4s  ", local_data.gas_mq2, (local_data.air_mq135 > AIR_BAD) ? "POOR" : "GOOD");
        }

        // Đẩy thẳng ra màn hình
        lcd_put_cur(0, 0);
        lcd_send_string(row1);
        lcd_put_cur(1, 0);
        lcd_send_string(row2);
    }
}

// =========================================================================
// HÀM MAIN: KHỞI TẠO TỔNG THỂ
// =========================================================================
void app_main(void) {
    ESP_LOGI(TAG, "===== KHOI DONG HE THONG SMART ALARM =====");
    
    // 1. Khởi tạo bộ nhớ Flash (NVS) để lưu thông tin WiFi
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // 2. Khởi tạo dịch vụ Network & RainMaker
    app_network_init();

    esp_rmaker_config_t rainmaker_cfg = { .enable_time_sync = true };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&rainmaker_cfg, "Smart Alarm ESP32", "Air Monitor");

    esp_rmaker_device_t *alarm_device = esp_rmaker_device_create("Monitor Node", NULL, NULL);
    esp_rmaker_device_add_cb(alarm_device, write_cb, NULL); // Đăng ký hàm nhận lệnh

    // Khởi tạo các tham số đẩy lên giao diện App (UI)
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

    buzzer_param = esp_rmaker_param_create("Buzzer State", "custom.param.mute", esp_rmaker_bool(false), PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_param_add_ui_type(buzzer_param, "esp.ui.toggle");
    esp_rmaker_device_add_param(alarm_device, buzzer_param); 

    // Gắn Device vào Node và khởi chạy RainMaker
    esp_rmaker_node_add_device(node, alarm_device);
    esp_rmaker_start();

    // Khởi chạy chế độ cấp quyền WiFi qua BLE (Provisioning)
    err = app_network_start(POP_TYPE_RANDOM);
    if (err != ESP_OK) ESP_LOGE(TAG, "Loi khoi dong Wi-Fi Provisioning");

    // 3. Khởi tạo tài nguyên FreeRTOS
    xMutex = xSemaphoreCreateMutex();

    // 4. Khởi tạo ngoại vi phần cứng
    lcd_i2c_init();
    lcd_clear();
    lcd_put_cur(0, 1);
    lcd_send_string("SYSTEM Khoi dong");

    mq_sensors_init(); // Khởi tạo ADC chung cho MQ2 và MQ135
    dht11_init();
    buzzer_init();

    // 5. Phân luồng chạy đa nhiệm
    // Tạo Task màn hình và ghim TaskHandle để nút nhấn gửi tín hiệu đến
    xTaskCreate(task_display_alarm, "LCD_Alarm_Task", 4096, NULL, 5, &xDisplayTask);
    button_init(xDisplayTask); 

    // Tạo Task đọc cảm biến độc lập
    xTaskCreate(task_sensor_read, "Sensor_Read_Task", 4096, NULL, 5, NULL);
}