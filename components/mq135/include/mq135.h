#ifndef MQ135_H
#define MQ135_H

#include "hal/adc_types.h"

// Sử dụng Unit 2, Kênh 0 (Tương ứng với chân GPIO 5 trên ESP32-C3)
#define MQ135_ADC_UNIT ADC_UNIT_1
#define MQ135_ADC_CHAN ADC_CHANNEL_3

void mq135_init(void);
int mq135_read_raw(void);
int mq135_get_air_quality(void); // Trả về % ô nhiễm không khí

#endif