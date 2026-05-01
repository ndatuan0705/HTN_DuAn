#ifndef LCD_I2C_H
#define LCD_I2C_H

#include "driver/i2c.h"

// Cấu hình phần cứng
#define I2C_MASTER_SDA_IO   1    // Chân SDA 
#define I2C_MASTER_SCL_IO   2    // Chân SCL 
#define I2C_MASTER_NUM      I2C_NUM_0
#define LCD_ADDR            0x27 

esp_err_t lcd_i2c_init(void);      // Khởi tạo I2C và LCD
void lcd_clear(void);              // Xóa màn hình
void lcd_put_cur(int row, int col); // Chuyển con trỏ (row 0-1, col 0-15)
void lcd_send_string(char *str);   // In chuỗi

#endif