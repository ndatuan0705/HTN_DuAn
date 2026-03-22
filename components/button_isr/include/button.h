#ifndef BUTTON_H
#define BUTTON_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

// Chân cắm nút nhấn của bạn (Sửa nếu cần)
#define BUTTON_PIN GPIO_NUM_5 

// Hàm khởi tạo, truyền vào Handle của Task cần nhận tín hiệu
void button_init(TaskHandle_t task_to_notify);

#endif