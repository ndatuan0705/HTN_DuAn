#include "lcd_i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rom/ets_sys.h"
#include "driver/i2c.h"

// --- Định nghĩa các bit điều khiển (Giao tiếp qua chip PCF8574) ---
// Chip PCF8574 nhận 1 byte (8 bit) qua I2C và xuất ra 8 chân (P0-P7) nối với LCD
// P0 = RS | P1 = RW | P2 = EN | P3 = Backlight | P4-P7 = D4-D7
#define LCD_RS      0x01  // Bit 0: Register Select (0: Gửi Lệnh/Command, 1: Gửi Ký tự/Data)
#define LCD_EN      0x04  // Bit 2: Enable (Xung chốt dữ liệu, LCD chỉ đọc data khi chân này có xung)
#define LCD_BL      0x08  // Bit 3: Backlight (Kéo mức 1 để bật đèn nền màn hình)

// =========================================================================
// HÀM GHI NIBBLE (4-BIT) XUỐNG LCD
// (Bản chất: Gửi 4 bit dữ liệu kết hợp tạo xung Enable để LCD "chụp" dữ liệu)
// =========================================================================
static void lcd_write_nibble(uint8_t nibble, uint8_t mode) {
    // Trộn 4 bit cao của dữ liệu + Cờ RS (Lệnh hay Ký tự) + Cờ bật đèn nền
    uint8_t data_out = (nibble & 0xF0) | mode | LCD_BL; 

    // BƯỚC 1: Đưa dữ liệu lên đường truyền (Lúc này EN đang = 0)
    // portMAX_DELAY: Cho phép I2C chờ vô hạn đến khi gửi thành công
    i2c_master_write_to_device(I2C_MASTER_NUM, LCD_ADDR, &data_out, 1, portMAX_DELAY);
    
    // BƯỚC 2: Tạo sườn dương (Kéo EN lên 1)
    data_out |= LCD_EN; 
    i2c_master_write_to_device(I2C_MASTER_NUM, LCD_ADDR, &data_out, 1, portMAX_DELAY);
    ets_delay_us(50);   // Đợi 50us (Setup time) cho điện áp ổn định để chip LCD sẵn sàng đọc

    // BƯỚC 3: Tạo sườn âm (Kéo EN về 0 để hoàn thành xung chốt)
    // Khi EN rơi từ 1 xuống 0, chip HD44780 của LCD chính thức "nuốt" dữ liệu vào bộ nhớ
    data_out &= ~LCD_EN; 
    i2c_master_write_to_device(I2C_MASTER_NUM, LCD_ADDR, &data_out, 1, portMAX_DELAY);
    ets_delay_us(50);   // Đợi 50us (Hold time) để LCD xử lý xong tín hiệu vừa nhận
}

// =========================================================================
// HÀM GỬI LỆNH HOẶC DỮ LIỆU (Tách 1 Byte thành 2 Nibble)
// (Vì ta đang dùng giao tiếp 4-bit, nên 1 byte (8-bit) phải chia làm 2 nửa)
// =========================================================================
static void lcd_send(uint8_t value, uint8_t mode) {
    // 1. Gửi 4 bit cao trước (MSB First)
    lcd_write_nibble(value & 0xF0, mode);        
    // 2. Dịch 4 bit thấp lên cao rồi gửi tiếp
    lcd_write_nibble((value << 4) & 0xF0, mode); 
    
    // Nếu là gửi Lệnh (mode == 0), thường LCD cần nhiều thời gian để thực thi phần cứng hơn 
    // so với việc in ký tự (mode == 1), nên ta thêm delay 2ms để an toàn.
    if(mode == 0) vTaskDelay(pdMS_TO_TICKS(2));  
}

// =========================================================================
// KHỞI TẠO MÀN HÌNH LCD VÀ MODULE I2C MASTER
// =========================================================================
esp_err_t lcd_i2c_init(void) {
    // 1. Cấu hình ngoại vi I2C Master của ESP32
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,           // Chế độ Master điều khiển Slave (LCD)
        .sda_io_num = I2C_MASTER_SDA_IO,   // Chân Data
        .scl_io_num = I2C_MASTER_SCL_IO,   // Chân Clock
        .sda_pullup_en = GPIO_PULLUP_ENABLE, // Bật điện trở kéo lên nội bộ (Bắt buộc với I2C)
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,        // Ép tốc độ 100kHz (Standard Mode) để truyền xa, chống nhiễu
    };
    i2c_param_config(I2C_MASTER_NUM, &conf);
    esp_err_t err = i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
    if(err != ESP_OK) return err; // Thoát nếu I2C lỗi

    vTaskDelay(pdMS_TO_TICKS(50)); // Đợi điện áp màn hình hoàn toàn ổn định sau khi cấp nguồn

    // 2. Quy trình Reset mềm ép LCD vào chế độ 4-bit
    // Bắt buộc tuân thủ đúng Datasheet của chip HD44780 (Initialization by Instruction)
    lcd_write_nibble(0x30, 0); vTaskDelay(pdMS_TO_TICKS(5)); // Gửi 0x30 lần 1, đợi > 4.1ms
    lcd_write_nibble(0x30, 0); ets_delay_us(200);            // Gửi 0x30 lần 2, đợi > 100us
    lcd_write_nibble(0x30, 0); ets_delay_us(200);            // Gửi 0x30 lần 3
    lcd_write_nibble(0x20, 0); ets_delay_us(200);            // Gửi 0x20: Chính thức khóa chế độ 4-bit

    // 3. Thiết lập các thông số hoạt động cơ bản của LCD
    // Gửi lệnh 0x28: Function Set (Chế độ 4-bit, hiển thị 2 dòng, font chữ 5x8 điểm ảnh)
    lcd_send(0x28, 0); 
    // Gửi lệnh 0x0C: Display Control (Bật toàn màn hình, Tắt con trỏ nhấp nháy cho đẹp)
    lcd_send(0x0C, 0); 
    // Gửi lệnh 0x06: Entry Mode Set (Sau mỗi lần in 1 chữ, tự động dịch con trỏ sang phải 1 ô)
    lcd_send(0x06, 0); 
    
    lcd_clear(); // Xóa sạch rác khởi động trên màn hình
    
    return ESP_OK;
}

// =========================================================================
// HÀM XÓA TRẮNG MÀN HÌNH
// =========================================================================
void lcd_clear(void) {
    // 0x01 là lệnh Clear Display theo chuẩn HD44780
    lcd_send(0x01, 0);
    // Lệnh xóa cần xóa toàn bộ bộ nhớ DDRAM của LCD nên rất tốn thời gian, bắt buộc delay > 1.5ms
    vTaskDelay(pdMS_TO_TICKS(2));
}

// =========================================================================
// HÀM DI CHUYỂN CON TRỎ (SET CURSOR)
// =========================================================================
void lcd_put_cur(int row, int col) {
    // Địa chỉ bắt đầu của dòng 1 (row 0) là 0x80
    // Địa chỉ bắt đầu của dòng 2 (row 1) là 0xC0
    // Lấy địa chỉ gốc cộng với số thứ tự cột (col) để ra vị trí đích
    int addr = (row == 0) ? (0x80 + col) : (0xC0 + col);
    lcd_send(addr, 0); // Gửi lệnh đặt địa chỉ
}

// =========================================================================
// HÀM IN CHUỖI VĂN BẢN
// =========================================================================
void lcd_send_string(char *str) {
    // Lặp qua từng ký tự của chuỗi cho đến khi gặp ký tự kết thúc chuỗi '\0'
    while (*str) {
        // Gửi từng ký tự với mode = LCD_RS (tức là 1) để báo cho LCD biết đây là data hiển thị
        lcd_send(*str++, LCD_RS); 
    }
}