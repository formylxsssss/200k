/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32u5xx_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32u5xx_it.h"

/* USER CODE BEGIN 0 */
#include "acquisition.h"
#include "app_log.h"
#include "sdmmc.h"

/* USER CODE END 0 */

void NMI_Handler(void)
{
  while (1)
  {
  }
}

void HardFault_Handler(void)
{
  APP_LOG_ERR("HardFault CFSR=0x%08X HFSR=0x%08X BFAR=0x%08X MMFAR=0x%08X",
              (unsigned int)SCB->CFSR,
              (unsigned int)SCB->HFSR,
              (unsigned int)SCB->BFAR,
              (unsigned int)SCB->MMFAR);
  while (1)
  {
  }
}

void MemManage_Handler(void)
{
  while (1)
  {
  }
}

void BusFault_Handler(void)
{
  while (1)
  {
  }
}

void UsageFault_Handler(void)
{
  while (1)
  {
  }
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

void SysTick_Handler(void)
{
  HAL_IncTick();
  Acquisition_WatchdogTick(HAL_GetTick());
}

void EXTI1_IRQHandler(void)
{
  __HAL_GPIO_EXTI_CLEAR_IT(ADS_DRDY_Pin);
  Acquisition_DrdyIsr();
}

void SDMMC1_IRQHandler(void)
{
  HAL_SD_IRQHandler(&hsd1);
}

void SDMMC2_IRQHandler(void)
{
  HAL_SD_IRQHandler(&hsd2);
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
