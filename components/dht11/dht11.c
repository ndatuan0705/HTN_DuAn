#include "dht11.h"
#include "rom/ets_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Hàm nội bộ: Chờ mức logic chuyển đổi kèm theo giới hạn thời gian (Timeout)
static int wait_for_state(int state, int timeout_us) {
    int t = 0;
    while (gpio_get_level(DHT11_PIN) != state) {
        if (t > timeout_us) return -1; 
        ets_delay_us(1);
        t++;
    }
    return t;
}

void dht11_init(void) {
    gpio_reset_pin(DHT11_PIN);
    // Dùng chế độ Open-Drain rất an toàn cho chuẩn 1-Wire
    gpio_set_direction(DHT11_PIN, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_level(DHT11_PIN, 1); // Trạng thái nghỉ luôn là mức HIGH
}

dht11_reading_t dht11_read(void) {
    dht11_reading_t result = {0, 0, -1};
    uint8_t data[5] = {0, 0, 0, 0, 0};

    // 1. Kích hoạt cảm biến (Kéo LOW > 18ms)
    gpio_set_level(DHT11_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(20)); 
    gpio_set_level(DHT11_PIN, 1);
    ets_delay_us(30);

    // 2. Vào vùng cấm ngắt để bắt đầu đọc dữ liệu tốc độ cao
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    taskENTER_CRITICAL(&mux);

    // Chờ DHT11 phản hồi (LOW 80us -> HIGH 80us -> LOW)
    if (wait_for_state(0, 80) == -1) { taskEXIT_CRITICAL(&mux); return result; }
    if (wait_for_state(1, 80) == -1) { taskEXIT_CRITICAL(&mux); return result; }
    if (wait_for_state(0, 80) == -1) { taskEXIT_CRITICAL(&mux); return result; }

    // 3. Đọc 40 bit dữ liệu (5 byte)
    for (int i = 0; i < 40; i++) {
        if (wait_for_state(1, 60) == -1) { taskEXIT_CRITICAL(&mux); return result; }
        
        // Đo thời gian bit HIGH
        int high_time = wait_for_state(0, 100);
        if (high_time == -1) { taskEXIT_CRITICAL(&mux); return result; }

        // Mức HIGH kéo dài > 40us là bit 1, ngược lại là bit 0
        if (high_time > 40) {
            data[i / 8] |= (1 << (7 - (i % 8)));
        }
    }

    // Ra khỏi vùng cấm ngắt
    taskEXIT_CRITICAL(&mux);
    gpio_set_level(DHT11_PIN, 1);

    // 4. Kiểm tra toàn vẹn dữ liệu
    uint8_t checksum = data[0] + data[1] + data[2] + data[3];
    if (checksum != data[4]) {
        result.status = -2; 
        return result;
    }

    // 5. Gán dữ liệu (Byte 0: Độ ẩm, Byte 2: Nhiệt độ)
    result.humidity = data[0];
    result.temperature = data[2];
    result.status = 0;

    return result;
}