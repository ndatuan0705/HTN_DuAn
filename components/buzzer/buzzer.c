#include "buzzer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void buzzer_init(void) {
    gpio_reset_pin(BUZZER_PIN);
    gpio_set_direction(BUZZER_PIN, GPIO_MODE_OUTPUT);
    
    // Cực kỳ quan trọng: Với Low-level trigger, phải set mức 1 (HIGH) ngay lập tức 
    // để còi KHÔNG bị kêu ré lên khi ESP32-C3 vừa khởi động
    gpio_set_level(BUZZER_PIN, 1); 
}

void buzzer_on(void) {
    // Mức 0 (LOW) đóng mạch -> Còi kêu
    gpio_set_level(BUZZER_PIN, 0); 
}

void buzzer_off(void) {
    // Mức 1 (HIGH) ngắt mạch -> Còi tắt
    gpio_set_level(BUZZER_PIN, 1); 
}

// Hàm tiện ích: Còi kêu một tiếng bíp ngắn rồi tắt
void buzzer_beep(int duration_ms) {
    buzzer_on();
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    buzzer_off();
}