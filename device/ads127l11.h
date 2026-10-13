#ifndef __ADS127L11_H__
#define __ADS127L11_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "stm32u5xx_hal.h"
#include "system_config.h"

typedef struct
{
  SPI_HandleTypeDef *spi;
  uint8_t sample_bytes;
} ads127l11_t;

HAL_StatusTypeDef ADS127L11_Init(ads127l11_t *dev, SPI_HandleTypeDef *spi);
HAL_StatusTypeDef ADS127L11_HardwareReset(ads127l11_t *dev);
HAL_StatusTypeDef ADS127L11_ConfigureDefault(ads127l11_t *dev);
HAL_StatusTypeDef ADS127L11_ConfigureAcquisition(ads127l11_t *dev, const acquisition_settings_t *settings);
HAL_StatusTypeDef ADS127L11_ReadSample(ads127l11_t *dev, int32_t *sample);
HAL_StatusTypeDef ADS127L11_ReadSampleFast(ads127l11_t *dev, int32_t *sample);
void ADS127L11_SyncPulse(void);
bool ADS127L11_DataReadyLevel(void);

#ifdef __cplusplus
}
#endif

#endif /* __ADS127L11_H__ */
