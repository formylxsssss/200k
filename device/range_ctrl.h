#ifndef __RANGE_CTRL_H__
#define __RANGE_CTRL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "app_config.h"

void RangeCtrl_SetSwitchBits(uint8_t bits);
uint8_t RangeCtrl_GetGpioLevels(void);

#ifdef __cplusplus
}
#endif

#endif /* __RANGE_CTRL_H__ */
