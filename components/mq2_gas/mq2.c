#include "mq2.h"
#include "esp_adc/adc_oneshot.h"

// Biến dùng chung cho toàn dự án
adc_oneshot_unit_handle_t shared_adc1_handle = NULL;

void mq2_init(void) {
    adc_oneshot_unit_init_cfg_t init_config = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &shared_adc1_handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(shared_adc1_handle, MQ2_ADC_CHAN, &config));
}

int mq2_get_percentage(void) {
    int raw = 0;
    adc_oneshot_read(shared_adc1_handle, MQ2_ADC_CHAN, &raw);
    int percent = ((raw - 400) * 100) / (4000 - 400);
    return (percent < 0) ? 0 : (percent > 100 ? 100 : percent);
}