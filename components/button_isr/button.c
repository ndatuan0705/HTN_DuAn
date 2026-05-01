#include "button.h"
#include "esp_attr.h"

static TaskHandle_t isr_notify_task = NULL;
static uint32_t last_isr_time = 0; // Lưu thời điểm nhấn nút cuối cùng

// Hàm Ngắt: BẮT BUỘC phải có IRAM_ATTR để nạp thẳng vào RAM, giúp chạy tốc độ cực cao
static void IRAM_ATTR button_isr_handler(void* arg) {
    // Lấy thời gian hiện tại của hệ thống (tính bằng Ticks)
    uint32_t current_time = xTaskGetTickCountFromISR();
    
    // KỸ THUẬT CHỐNG DỘI PHÍM (DEBOUNCE): 
    // Chỉ chấp nhận tín hiệu ngắt nếu khoảng cách giữa 2 lần nhấn > 300ms
    // Điều này chặn đứng hoàn toàn "Interrupt Storm" gây sập chip
    if ((current_time - last_isr_time) > pdMS_TO_TICKS(300)) {
        
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        
        // Gửi tín hiệu đánh thức Task Alarm
        if (isr_notify_task != NULL) {
            vTaskNotifyGiveFromISR(isr_notify_task, &xHigherPriorityTaskWoken);
        }
        
        last_isr_time = current_time; // Cập nhật lại thời gian
        
        if (xHigherPriorityTaskWoken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    }
}

void button_init(TaskHandle_t task_to_notify) {
    isr_notify_task = task_to_notify;

    // Cấu hình chân nút nhấn (Kéo lên mức HIGH)
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE // Kích hoạt ngắt ở sườn âm (Khi nhấn nút xuống GND)
    };
    gpio_config(&btn_cfg);

    // Cài đặt dịch vụ Ngắt toàn cục của ESP32
    gpio_install_isr_service(0);
    
    // Gắn hàm xử lý ngắt vào chân nút nhấn
    gpio_isr_handler_add(BUTTON_PIN, button_isr_handler, NULL);
}