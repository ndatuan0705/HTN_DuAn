#include "mq135.h"
#include "esp_adc/adc_oneshot.h"

// Mượn handle từ mq2.c
extern adc_oneshot_unit_handle_t shared_adc1_handle;

void mq135_init(void) {
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    // Chỉ cấu hình channel, KHÔNG khởi tạo unit lần nữa
    ESP_ERROR_CHECK(adc_oneshot_config_channel(shared_adc1_handle, MQ135_ADC_CHAN, &config));
}

int mq135_get_air_quality(void) {
    int raw = 0;
    adc_oneshot_read(shared_adc1_handle, MQ135_ADC_CHAN, &raw);
    int percent = ((raw - 400) * 100) / (4000 - 400);
    return (percent < 0) ? 0 : (percent > 100 ? 100 : percent);
}