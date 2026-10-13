#ifndef __TF_SLOTS_H__
#define __TF_SLOTS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32u5xx_hal.h"
#include <stdbool.h>

typedef enum
{
  TF_SLOT_1 = 1,
  TF_SLOT_2 = 2,
  TF_SLOT_3 = 3,
  TF_SLOT_4 = 4,
} tf_slot_t;

HAL_StatusTypeDef TFSlot_Select(tf_slot_t slot);
HAL_StatusTypeDef TFSlot_Power(tf_slot_t slot, GPIO_PinState state);
HAL_StatusTypeDef TFSlot_InitSelected(tf_slot_t slot);
HAL_StatusTypeDef TFSlot_ProbeSelected(tf_slot_t slot);
void TFSlot_AllOff(void);

#ifdef __cplusplus
}
#endif

#endif /* __TF_SLOTS_H__ */
