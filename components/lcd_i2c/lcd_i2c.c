#include "lcd_i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rom/ets_sys.h"
#include "driver/i2c.h"

// --- Định nghĩa các bit điều khiển IC PCF8574 ---
#define LCD_RS      0x01  // Bit 0: Register Select (0 = Lệnh, 1 = Dữ liệu)
#define LCD_RW      0x02  // Bit 1: Read/Write (Luôn = 0 để Ghi)
#define LCD_EN      0x04  // Bit 2: Enable (Xung chốt dữ liệu)
#define LCD_BL      0x08  // Bit 3: Backlight (1 = Bật đèn nền)

// Khai báo các hàm nội bộ
static void lcd_send_cmd(char cmd);
static void lcd_send_data(char data);
static void lcd_write_nibble(uint8_t nibble, uint8_t rs);

// =========================================================================
// 1. HÀM GHI DỮ LIỆU CẤP THẤP (BẢN CHỐNG NHIỄU TUYỆT ĐỐI 3 BƯỚC)
// =========================================================================
static void lcd_write_nibble(uint8_t nibble, uint8_t rs) {
    uint8_t data_out;

    // BƯỚC 1: Đưa dữ liệu lên đường truyền (Giữ EN = 0)
    // Giúp chip LCD có thời gian đọc và ổn định tín hiệu
    data_out = nibble | rs | LCD_BL; 
    i2c_master_write_to_device(I2C_MASTER_NUM, LCD_ADDR, &data_out, 1, pdMS_TO_TICKS(10));
    ets_delay_us(50); 

    // BƯỚC 2: Kéo xung EN lên 1 (Bấm máy chụp ảnh)
    data_out |= LCD_EN; 
    i2c_master_write_to_device(I2C_MASTER_NUM, LCD_ADDR, &data_out, 1, pdMS_TO_TICKS(10));
    ets_delay_us(200); // Giữ xung 200us để đảm bảo LCD nhận đủ điện áp

    // BƯỚC 3: Kéo xung EN về 0 (Chốt dữ liệu xong)
    data_out &= ~LCD_EN; 
    i2c_master_write_to_device(I2C_MASTER_NUM, LCD_ADDR, &data_out, 1, pdMS_TO_TICKS(10));
    ets_delay_us(200); // Đợi LCD tiêu hóa dữ liệu trước khi gửi tiếp
}

// =========================================================================
// 2. HÀM GỬI LỆNH (COMMAND)
// =========================================================================
static void lcd_send_cmd(char cmd) {
    lcd_write_nibble(cmd & 0xF0, 0);         // Gửi 4 bit cao
    lcd_write_nibble((cmd << 4) & 0xF0, 0);  // Gửi 4 bit thấp
    vTaskDelay(pdMS_TO_TICKS(1));            // Nghỉ ngắn giữa các lệnh
}

// =========================================================================
// 3. HÀM GỬI KÝ TỰ (DATA)
// =========================================================================
static void lcd_send_data(char data) {
    lcd_write_nibble(data & 0xF0, LCD_RS);         // Gửi 4 bit cao với RS=1
    lcd_write_nibble((data << 4) & 0xF0, LCD_RS);  // Gửi 4 bit thấp với RS=1
}

// =========================================================================
// 4. HÀM KHỞI TẠO LCD & I2C
// =========================================================================
esp_err_t lcd_i2c_init(void) {
    // 1. Cấu hình phần cứng I2C (An toàn với 100kHz)
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE, // Bắt buộc bật trở kéo lên
        .scl_pullup_en = GPIO_PULLUP_ENABLE, // Bắt buộc bật trở kéo lên
        .master.clk_speed = 100000,          // Ép tốc độ 100kHz để chống nhiễu
    };
    i2c_param_config(I2C_MASTER_NUM, &conf);
    esp_err_t err = i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
    if(err != ESP_OK) return err;

    vTaskDelay(pdMS_TO_TICKS(100)); // Đợi nguồn ổn định hoàn toàn

    // 2. Trình tự Reset mềm theo chuẩn Datasheet HD44780
    lcd_write_nibble(0x30, 0); vTaskDelay(pdMS_TO_TICKS(5));
    lcd_write_nibble(0x30, 0); vTaskDelay(pdMS_TO_TICKS(1));
    lcd_write_nibble(0x30, 0); vTaskDelay(pdMS_TO_TICKS(1));
    lcd_write_nibble(0x20, 0); vTaskDelay(pdMS_TO_TICKS(1)); // Chuyển sang chế độ 4-bit

    // 3. Cấu hình hiển thị
    lcd_send_cmd(0x28); // 2 dòng, font 5x8, 4-bit mode
    lcd_send_cmd(0x0C); // Bật màn hình, tắt con trỏ
    lcd_send_cmd(0x06); // Tự động dịch con trỏ sang phải
    lcd_clear();
    
    return ESP_OK;
}

// =========================================================================
// 5. CÁC HÀM TIỆN ÍCH DÀNH CHO MAIN.C
// =========================================================================
void lcd_clear(void) {
    lcd_send_cmd(0x01); // Lệnh xóa toàn bộ màn hình
    vTaskDelay(pdMS_TO_TICKS(2)); // Lệnh này rất chậm, phải đợi ít nhất 2ms
}

void lcd_put_cur(int row, int col) {
    int addr = (row == 0) ? (0x80 + col) : (0xC0 + col);
    lcd_send_cmd(addr);
}

void lcd_send_string(char *str) {
    while (*str) {
        lcd_send_data(*str++);
        // Nghỉ 500us giữa TỪNG CHỮ CÁI để chống dính chữ
        ets_delay_us(500); 
    }
}