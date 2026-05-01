#include "dht11.h"
#include "rom/ets_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// =========================================================================
// HÀM CHỜ TRẠNG THÁI (TIMEOUT)
// =========================================================================
// Hàm này đợi chân tín hiệu chuyển sang mức (state) mong muốn.
// Nếu đợi quá thời gian (timeout_us) mà chưa thấy đổi -> Báo lỗi (-1)
static int wait_for_state(int state, int timeout_us) {
    int t = 0;
    while (gpio_get_level(DHT11_PIN) != state) {
        if (t > timeout_us) return -1; 
        ets_delay_us(1);
        t++;
    }
    return t; // Trả về thời gian đã phải chờ (micro-giây)
}

// =========================================================================
// KHỞI TẠO CHÂN DHT11
// =========================================================================
void dht11_init(void) {
    gpio_reset_pin(DHT11_PIN);
    // Cực kỳ quan trọng: Chế độ Open-Drain (Cực máng hở).
    // Giúp cả ESP32 và DHT11 có thể kéo dây tín hiệu xuống GND mà không gây chập mạch.
    gpio_set_direction(DHT11_PIN, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_level(DHT11_PIN, 1); // Trạng thái nghỉ (Idle) của bus 1-Wire luôn là mức CAO
}

// =========================================================================
// HÀM ĐỌC DỮ LIỆU
// =========================================================================
dht11_reading_t dht11_read(void) {
    dht11_reading_t result = {0, 0, -1};
    uint8_t data[5] = {0, 0, 0, 0, 0}; // Mảng chứa 40 bit (5 byte) dữ liệu

    // 1. Gửi tín hiệu Start (Kéo LOW > 18ms để đánh thức DHT11)
    gpio_set_level(DHT11_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(20)); // Chờ 20ms
    gpio_set_level(DHT11_PIN, 1);  // Thả bus ra, chuẩn bị đọc
    ets_delay_us(30);

    // 2. VÀO VÙNG CẤM NGẮT (CRITICAL SECTION)
    // Tạm dừng mọi hoạt động chuyển task của FreeRTOS để đo thời gian micro-giây chính xác tuyệt đối
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    taskENTER_CRITICAL(&mux);

    // Bắt đầu chuỗi Handshake của DHT11: LOW (80us) -> HIGH (80us)
    if (wait_for_state(0, 80) == -1) { taskEXIT_CRITICAL(&mux); return result; }
    if (wait_for_state(1, 80) == -1) { taskEXIT_CRITICAL(&mux); return result; }
    if (wait_for_state(0, 80) == -1) { taskEXIT_CRITICAL(&mux); return result; }

    // 3. Đọc 40 bit dữ liệu (5 byte = Độ ẩm nguyên + Độ ẩm thập phân + Nhiệt nguyên + Nhiệt thập phân + Checksum)
    for (int i = 0; i < 40; i++) {
        // Chờ kết thúc khoảng trễ LOW 50us trước mỗi bit
        if (wait_for_state(1, 60) == -1) { taskEXIT_CRITICAL(&mux); return result; }
        
        // Bắt đầu đo độ rộng của xung HIGH để phân biệt bit 0 hay bit 1
        int high_time = wait_for_state(0, 100);
        if (high_time == -1) { taskEXIT_CRITICAL(&mux); return result; }

        // Datasheet DHT11: Xung HIGH dài ~26us là Bit '0', dài ~70us là Bit '1'
        // Chọn mốc 40us làm ranh giới phân biệt
        if (high_time > 40) {
            data[i / 8] |= (1 << (7 - (i % 8))); // Set bit tương ứng lên 1
        }
    }

    // 4. RA KHỎI VÙNG CẤM NGẮT
    taskEXIT_CRITICAL(&mux);
    gpio_set_level(DHT11_PIN, 1); // Trả bus về trạng thái nghỉ

    // 5. Kiểm tra toàn vẹn dữ liệu (Checksum)
    uint8_t checksum = data[0] + data[1] + data[2] + data[3];
    if (checksum != data[4]) {
        result.status = -2; // Lỗi Checksum (Do nhiễu đường truyền)
        return result;
    }

    // 6. Gán dữ liệu thành công (Với DHT11, byte 1 và 3 luôn bằng 0)
    result.humidity = data[0];
    result.temperature = data[2];
    result.status = 0;

    return result;
}