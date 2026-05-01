#ifndef BUZZER_H
#define BUZZER_H

#include "driver/gpio.h"

#define BUZZER_PIN  GPIO_NUM_6 // Bạn có thể đổi sang chân khác tùy ý

void buzzer_init(void);
void buzzer_on(void);
void buzzer_off(void);
void buzzer_beep(int duration_ms);

#endif