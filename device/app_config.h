#ifndef __APP_CONFIG_H__
#define __APP_CONFIG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "main.h"

#define APP_ADC_SAMPLE_RATE_HZ       200000u
#define APP_ADC_CLK_PER_SAMPLE       64u
#define APP_ADC_SPI_TIMEOUT_MS       10u
#define APP_W5500_SPI_DELAY_CYCLES   4u

#define BOARD_TF_SEL_BANK0           GPIO_PIN_RESET
#define BOARD_TF_SEL_BANK1           GPIO_PIN_SET

typedef enum
{
  APP_ATTENUATION_0DB = 0,
  APP_ATTENUATION_MINUS_10DB,
  APP_ATTENUATION_MINUS_30DB,
} app_attenuation_t;

#define APP_RANGE_SWITCH_D0            0x01u
#define APP_RANGE_SWITCH_D1            0x02u
#define APP_RANGE_SWITCH_D2            0x04u
#define APP_RANGE_SWITCH_D3            0x08u

#define APP_DEFAULT_ATTENUATION        APP_ATTENUATION_0DB
#define APP_DEFAULT_RANGE_SWITCH_BITS  APP_RANGE_SWITCH_D0

typedef struct
{
  uint8_t mac[6];
  uint8_t ip[4];
  uint8_t gateway[4];
  uint8_t netmask[4];
} app_net_config_t;

const app_net_config_t *AppConfig_GetNet(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_CONFIG_H__ */
