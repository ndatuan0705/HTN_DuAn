#ifndef DHT11_H
#define DHT11_H

#include "driver/gpio.h"

// Cấu hình chân cắm
#define DHT11_PIN GPIO_NUM_4

// Cấu trúc dữ liệu trả về
typedef struct {
    int temperature;
    int humidity;
    int status; // 0: OK, -1: Lỗi Timeout (mất kết nối), -2: Lỗi Checksum (nhiễu)
} dht11_reading_t;

void dht11_init(void);
dht11_reading_t dht11_read(void);

#endif