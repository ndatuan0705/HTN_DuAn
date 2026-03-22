#ifndef MQ2_H
#define MQ2_H

#include "hal/adc_types.h"

// Định nghĩa phần cứng cho ESP32-C3
#define MQ2_ADC_UNIT ADC_UNIT_1
#define MQ2_ADC_CHAN ADC_CHANNEL_0 // Kênh 0 tương ứng với chân GPIO 0

void mq2_init(void);
int mq2_read_raw(void);

// Hàm tính toán % khí gas (tùy chọn để hiển thị cho đẹp)
int mq2_get_percentage(void); 

#endif