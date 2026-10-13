#include "range_ctrl.h"
#include "app_log.h"

static GPIO_PinState RangeCtrl_BranchToGpio(uint8_t bits, uint8_t mask)
{
  return ((bits & mask) != 0u) ? GPIO_PIN_RESET : GPIO_PIN_SET;
}

uint8_t RangeCtrl_GetGpioLevels(void)
{
  uint8_t levels = 0u;

  if (HAL_GPIO_ReadPin(CH1_D0_GPIO_Port, CH1_D0_Pin) == GPIO_PIN_SET)
  {
    levels |= APP_RANGE_SWITCH_D0;
  }
  if (HAL_GPIO_ReadPin(CH1_D1_GPIO_Port, CH1_D1_Pin) == GPIO_PIN_SET)
  {
    levels |= APP_RANGE_SWITCH_D1;
  }
  if (HAL_GPIO_ReadPin(CH1_D2_GPIO_Port, CH1_D2_Pin) == GPIO_PIN_SET)
  {
    levels |= APP_RANGE_SWITCH_D2;
  }
  if (HAL_GPIO_ReadPin(CH1_D3_GPIO_Port, CH1_D3_Pin) == GPIO_PIN_SET)
  {
    levels |= APP_RANGE_SWITCH_D3;
  }

  return levels;
}

void RangeCtrl_SetSwitchBits(uint8_t bits)
{
  uint8_t selected = bits & 0x0Fu;
  uint8_t expected_levels = (uint8_t)(~selected) & 0x0Fu;
  uint8_t actual_levels;
  GPIO_PinState pb6 = RangeCtrl_BranchToGpio(bits, APP_RANGE_SWITCH_D0);
  GPIO_PinState pb7 = RangeCtrl_BranchToGpio(bits, APP_RANGE_SWITCH_D1);
  GPIO_PinState pb8 = RangeCtrl_BranchToGpio(bits, APP_RANGE_SWITCH_D2);
  GPIO_PinState pb9 = RangeCtrl_BranchToGpio(bits, APP_RANGE_SWITCH_D3);

  HAL_GPIO_WritePin(CH1_D0_GPIO_Port, CH1_D0_Pin, pb6);
  HAL_GPIO_WritePin(CH1_D1_GPIO_Port, CH1_D1_Pin, pb7);
  HAL_GPIO_WritePin(CH1_D2_GPIO_Port, CH1_D2_Pin, pb8);
  HAL_GPIO_WritePin(CH1_D3_GPIO_Port, CH1_D3_Pin, pb9);
  actual_levels = RangeCtrl_GetGpioLevels();

  if (actual_levels != expected_levels)
  {
    APP_LOG_ERR("gain GPIO verify failed selected=0x%X expected_PB9..PB6=0x%X actual=0x%X",
                (unsigned int)selected,
                (unsigned int)expected_levels,
                (unsigned int)actual_levels);
  }
  else
  {
    APP_LOG_INFO("gain switch selected=0x%X PB9..PB6=%u%u%u%u readback=ok",
                 (unsigned int)selected,
                 (unsigned int)((actual_levels >> 3u) & 0x01u),
                 (unsigned int)((actual_levels >> 2u) & 0x01u),
                 (unsigned int)((actual_levels >> 1u) & 0x01u),
                 (unsigned int)(actual_levels & 0x01u));
  }
}
