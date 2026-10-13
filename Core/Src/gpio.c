/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   This file provides code for the configuration of all used GPIO pins.
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "gpio.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure GPIO                                                             */
/*----------------------------------------------------------------------------*/
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, ADS_CS_Pin|ADS_SYNC_Pin|TF1_PWR_Pin|TF_SEL1_Pin
                         |W5500_SCLK_Pin|W5500_MOSI_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, ADS_RESET_Pin|CH1_D0_Pin|CH1_D1_Pin|CH1_D2_Pin
                         |CH1_D3_Pin|TF2_PWR_Pin|TF3_PWR_Pin|TF4_PWR_Pin
                         |TF_SEL2_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, CH1_D1_Pin|CH1_D2_Pin|CH1_D3_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, W5500_RESET_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(GPIOC, W5500_CS_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(ADS_RESET_GPIO_Port, ADS_RESET_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(ADS_CS_GPIO_Port, ADS_CS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin : ADS_DRDY_Pin */
  GPIO_InitStruct.Pin = ADS_DRDY_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(ADS_DRDY_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : ADS_CS_Pin ADS_SYNC_Pin TF1_PWR_Pin TF_SEL1_Pin W5500_SCLK_Pin W5500_MOSI_Pin */
  GPIO_InitStruct.Pin = ADS_CS_Pin|ADS_SYNC_Pin|TF1_PWR_Pin|TF_SEL1_Pin
                      |W5500_SCLK_Pin|W5500_MOSI_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : ADS_RESET_Pin CH1_D0_Pin CH1_D1_Pin CH1_D2_Pin CH1_D3_Pin TFx_PWR/SEL */
  GPIO_InitStruct.Pin = ADS_RESET_Pin|CH1_D0_Pin|CH1_D1_Pin|CH1_D2_Pin
                      |CH1_D3_Pin|TF2_PWR_Pin|TF3_PWR_Pin|TF4_PWR_Pin
                      |TF_SEL2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : W5500_RESET_Pin W5500_CS_Pin */
  GPIO_InitStruct.Pin = W5500_RESET_Pin|W5500_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /* PC4/PC5 are no longer part of gain control; keep them high impedance. */
  GPIO_InitStruct.Pin = GPIO_PIN_4|GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : EXT_TRIGGER_Pin */
  GPIO_InitStruct.Pin = EXT_TRIGGER_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(EXT_TRIGGER_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : W5500_MISO_Pin W5500_INT_Pin */
  GPIO_InitStruct.Pin = W5500_MISO_Pin|W5500_INT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI1_IRQn, 1, 0);
  HAL_NVIC_DisableIRQ(EXTI1_IRQn);
}

/* USER CODE BEGIN 2 */

/* USER CODE END 2 */
