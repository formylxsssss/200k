#include "tf_slots.h"
#include "app_config.h"
#include "app_log.h"
#include "sdmmc.h"

static void TFSlot_SetPowerPin(tf_slot_t slot, GPIO_PinState state)
{
  switch (slot)
  {
    case TF_SLOT_1:
      HAL_GPIO_WritePin(TF1_PWR_GPIO_Port, TF1_PWR_Pin, state);
      break;

    case TF_SLOT_2:
      HAL_GPIO_WritePin(TF2_PWR_GPIO_Port, TF2_PWR_Pin, state);
      break;

    case TF_SLOT_3:
      HAL_GPIO_WritePin(TF3_PWR_GPIO_Port, TF3_PWR_Pin, state);
      break;

    case TF_SLOT_4:
      HAL_GPIO_WritePin(TF4_PWR_GPIO_Port, TF4_PWR_Pin, state);
      break;

    default:
      break;
  }
}

static void TFSlot_AllOffInternal(bool verbose)
{
  TFSlot_SetPowerPin(TF_SLOT_1, GPIO_PIN_RESET);
  TFSlot_SetPowerPin(TF_SLOT_2, GPIO_PIN_RESET);
  TFSlot_SetPowerPin(TF_SLOT_3, GPIO_PIN_RESET);
  TFSlot_SetPowerPin(TF_SLOT_4, GPIO_PIN_RESET);
  if (verbose)
  {
    APP_LOG_INFO("TF all slots power off");
  }
}

static HAL_StatusTypeDef TFSlot_SelectInternal(tf_slot_t slot, bool verbose)
{
  switch (slot)
  {
    case TF_SLOT_1:
      HAL_GPIO_WritePin(TF_SEL1_GPIO_Port, TF_SEL1_Pin, BOARD_TF_SEL_BANK1);
      break;

    case TF_SLOT_2:
      HAL_GPIO_WritePin(TF_SEL1_GPIO_Port, TF_SEL1_Pin, BOARD_TF_SEL_BANK0);
      break;

    case TF_SLOT_3:
      HAL_GPIO_WritePin(TF_SEL2_GPIO_Port, TF_SEL2_Pin, BOARD_TF_SEL_BANK1);
      break;

    case TF_SLOT_4:
      HAL_GPIO_WritePin(TF_SEL2_GPIO_Port, TF_SEL2_Pin, BOARD_TF_SEL_BANK0);
      break;

    default:
      if (verbose)
      {
        APP_LOG_ERR("TF select invalid slot=%u", (unsigned int)slot);
      }
      return HAL_ERROR;
  }

  HAL_Delay(2);
  if (verbose)
  {
    APP_LOG_INFO("TF selected slot=%u", (unsigned int)slot);
  }
  return HAL_OK;
}

static HAL_StatusTypeDef TFSlot_InitSelectedInternal(tf_slot_t slot, bool verbose)
{
  SD_HandleTypeDef *sd = ((slot == TF_SLOT_1) || (slot == TF_SLOT_2)) ? &hsd1 : &hsd2;

  if (sd->State != HAL_SD_STATE_RESET)
  {
    (void)HAL_SD_DeInit(sd);
  }

  TFSlot_AllOffInternal(verbose);
  HAL_Delay(10);
  if (TFSlot_SelectInternal(slot, verbose) != HAL_OK)
  {
    if (verbose)
    {
      APP_LOG_ERR("TF select failed slot=%u", (unsigned int)slot);
    }
    return HAL_ERROR;
  }

  TFSlot_SetPowerPin(slot, GPIO_PIN_SET);
  HAL_Delay(50);
  if (verbose)
  {
    APP_LOG_INFO("TF slot=%u power=1", (unsigned int)slot);
  }

  if (HAL_SD_Init(sd) != HAL_OK)
  {
    if (verbose)
    {
      APP_LOG_ERR("TF HAL_SD_Init failed slot=%u err=0x%08X",
                  (unsigned int)slot,
                  (unsigned int)HAL_SD_GetError(sd));
    }
    return HAL_ERROR;
  }

  if (verbose)
  {
    APP_LOG_OK("TF init ok slot=%u", (unsigned int)slot);
  }
  return HAL_OK;
}

void TFSlot_AllOff(void)
{
  TFSlot_AllOffInternal(true);
}

HAL_StatusTypeDef TFSlot_Power(tf_slot_t slot, GPIO_PinState state)
{
  TFSlot_SetPowerPin(slot, state);
  HAL_Delay(20);
  APP_LOG_INFO("TF slot=%u power=%u", (unsigned int)slot, (unsigned int)(state == GPIO_PIN_SET));
  return HAL_OK;
}

HAL_StatusTypeDef TFSlot_Select(tf_slot_t slot)
{
  return TFSlot_SelectInternal(slot, true);
}

HAL_StatusTypeDef TFSlot_InitSelected(tf_slot_t slot)
{
  return TFSlot_InitSelectedInternal(slot, true);
}

HAL_StatusTypeDef TFSlot_ProbeSelected(tf_slot_t slot)
{
  return TFSlot_InitSelectedInternal(slot, false);
}
