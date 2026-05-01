#include "mq_sensors.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

// Khai báo cấu hình phần cứng
#define MQ2_ADC_CHAN   ADC_CHANNEL_0 // GPIO 0
#define MQ135_ADC_CHAN ADC_CHANNEL_3 // GPIO 3

// Dùng "static" để bảo vệ biến này, các file khác không thể truy cập, chống lỗi hoàn toàn
static adc_oneshot_unit_handle_t adc1_handle = NULL;

void mq_sensors_init(void) {
    // 1. Khởi tạo bộ ADC1 (Chỉ làm 1 lần duy nhất)
    adc_oneshot_unit_init_cfg_t init_config = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));

    // 2. Cấu hình chung cho cả 2 kênh
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    
    // Áp dụng cấu hình cho kênh MQ2 và MQ135
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, MQ2_ADC_CHAN, &config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, MQ135_ADC_CHAN, &config));
}

int mq2_get_gas_percentage(void) {
    int raw = 0;
    adc_oneshot_read(adc1_handle, MQ2_ADC_CHAN, &raw);
    int percent = ((raw - 400) * 100) / (4000 - 400);
    return (percent < 0) ? 0 : (percent > 100 ? 100 : percent);
}

int mq135_get_air_quality(void) {
    int raw = 0;
    adc_oneshot_read(adc1_handle, MQ135_ADC_CHAN, &raw);
    int percent = ((raw - 400) * 100) / (4000 - 400);
    return (percent < 0) ? 0 : (percent > 100 ? 100 : percent);
}