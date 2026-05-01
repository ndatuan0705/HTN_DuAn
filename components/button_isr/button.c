#include "button.h"
#include "esp_attr.h"

// --- BIẾN TOÀN CỤC NỘI BỘ (STATIC) ---
// isr_notify_task: Lưu "địa chỉ thẻ" của Task cần được đánh thức (ở đây là Task Màn hình/Còi)
static TaskHandle_t isr_notify_task = NULL;
// last_isr_time: Lưu lại mốc thời gian (tính bằng Tick) của lần nhấn nút hợp lệ cuối cùng
static uint32_t last_isr_time = 0; 

// =========================================================================
// HÀM PHỤC VỤ NGẮT (ISR - Interrupt Service Routine)
// =========================================================================
// IRAM_ATTR: Lệnh bắt buộc. Ép trình biên dịch nạp hàm này thẳng vào bộ nhớ RAM nội bộ (IRAM) 
// thay vì để ngoài bộ nhớ Flash. Khi ngắt xảy ra, CPU gọi hàm từ RAM sẽ đáp ứng tức thời, 
// tránh lỗi "Cache Miss" gây treo chip (Crash) khi đọc từ Flash do quá chậm.
static void IRAM_ATTR button_isr_handler(void* arg) {
    // Lấy thời gian hiện tại của hệ điều hành (tính bằng Tick).
    // Hậu tố "FromISR" biểu thị hàm này an toàn để sử dụng bên trong môi trường Ngắt.
    uint32_t current_time = xTaskGetTickCountFromISR();
    
    // --- KỸ THUẬT CHỐNG DỘI PHÍM BẰNG PHẦN MỀM (DEBOUNCE) ---
    // Nút nhấn cơ học có lò xo, 1 lần bấm tay tạo ra hàng chục xung nhiễu (nảy lên xuống).
    // Điều kiện này chỉ chấp nhận ngắt nếu khoảng cách giữa 2 lần kích hoạt > 300 mili-giây.
    // pdMS_TO_TICKS(300) đổi 300ms thành số Tick tương ứng của hệ thống.
    if ((current_time - last_isr_time) > pdMS_TO_TICKS(300)) {
        
        // Gửi tín hiệu đánh thức (Notify) đến Task đang chờ (Task Display)
        if (isr_notify_task != NULL) {
            // vTaskNotifyGiveFromISR là cơ chế "bắn" tín hiệu nhẹ và nhanh nhất trong FreeRTOS.
            // Tham số NULL ở cuối nghĩa là ta không yêu cầu CPU phải chuyển ngữ cảnh 
            // (Context Switch) ngay lập tức, giúp code đơn giản, giảm tải cho vi điều khiển.
            vTaskNotifyGiveFromISR(isr_notify_task, NULL); 
        }
        
        // Cập nhật lại mốc thời gian cho lần nhấn hợp lệ này
        last_isr_time = current_time; 
    }
}

// =========================================================================
// HÀM KHỞI TẠO NÚT NHẤN
// =========================================================================
void button_init(TaskHandle_t task_to_notify) {
    // Lưu lại Task cần đánh thức do hàm main truyền vào
    isr_notify_task = task_to_notify;

    // --- CẤU HÌNH CHÂN GPIO CHO NÚT NHẤN ---
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << BUTTON_PIN),  // Chọn chân GPIO (đã define trong button.h)
        .mode = GPIO_MODE_INPUT,               // Chế độ nhận tín hiệu đầu vào
        .pull_up_en = GPIO_PULLUP_ENABLE,      // Bật điện trở kéo lên nguồn (Trạng thái nghỉ luôn là HIGH - 1)
        .pull_down_en = GPIO_PULLDOWN_DISABLE, // Tắt điện trở kéo xuống đất
        // intr_type: Cấu hình kiểu kích hoạt ngắt. 
        // Khi nhấn nút, mạch nối đất (GND), điện áp tụt từ mức CAO (1) xuống mức THẤP (0).
        // Quá trình rơi xuống này gọi là sườn âm (Negative Edge).
        .intr_type = GPIO_INTR_NEGEDGE         
    };
    gpio_config(&btn_cfg); // Áp dụng cấu hình

    // Cài đặt dịch vụ Ngắt toàn cục của thư viện ESP-IDF. 
    // Số 0 là cờ mặc định (không yêu cầu quyền ưu tiên đặc biệt).
    gpio_install_isr_service(0);
    
    // Ánh xạ chân GPIO cụ thể với hàm phục vụ ngắt đã viết ở trên.
    // Bất cứ khi nào chân BUTTON_PIN rớt xuống 0, hàm button_isr_handler sẽ tự động chạy.
    gpio_isr_handler_add(BUTTON_PIN, button_isr_handler, NULL);
}