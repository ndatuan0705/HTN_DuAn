#ifndef MQ_SENSORS_H
#define MQ_SENSORS_H

void mq_sensors_init(void);
int mq2_get_gas_percentage(void);
int mq135_get_air_quality(void);

#endif