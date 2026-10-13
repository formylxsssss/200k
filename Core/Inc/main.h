/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  ******************************************************************************
  * @attention
  *
  * Generated for the STM32U575RGT6 single-channel 200 kS/s acquisition board.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32u5xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define ADS_DRDY_Pin GPIO_PIN_1
#define ADS_DRDY_GPIO_Port GPIOA
#define ADS_SYNC_Pin GPIO_PIN_2
#define ADS_SYNC_GPIO_Port GPIOA
#define ADS_CS_Pin GPIO_PIN_4
#define ADS_CS_GPIO_Port GPIOA
#define ADS_SCK_Pin GPIO_PIN_5
#define ADS_SCK_GPIO_Port GPIOA
#define ADS_MISO_Pin GPIO_PIN_6
#define ADS_MISO_GPIO_Port GPIOA
#define ADS_MOSI_Pin GPIO_PIN_7
#define ADS_MOSI_GPIO_Port GPIOA
#define ADS_CLK_Pin GPIO_PIN_0
#define ADS_CLK_GPIO_Port GPIOB
#define ADS_RESET_Pin GPIO_PIN_1
#define ADS_RESET_GPIO_Port GPIOB

#define EXT_TRIGGER_Pin GPIO_PIN_13
#define EXT_TRIGGER_GPIO_Port GPIOC

#define CH1_D0_Pin GPIO_PIN_6
#define CH1_D0_GPIO_Port GPIOB
#define CH1_D1_Pin GPIO_PIN_7
#define CH1_D1_GPIO_Port GPIOB
#define CH1_D2_Pin GPIO_PIN_8
#define CH1_D2_GPIO_Port GPIOB
#define CH1_D3_Pin GPIO_PIN_9
#define CH1_D3_GPIO_Port GPIOB

#define TF1_PWR_Pin GPIO_PIN_15
#define TF1_PWR_GPIO_Port GPIOA
#define TF2_PWR_Pin GPIO_PIN_5
#define TF2_PWR_GPIO_Port GPIOB
#define TF3_PWR_Pin GPIO_PIN_12
#define TF3_PWR_GPIO_Port GPIOB
#define TF4_PWR_Pin GPIO_PIN_10
#define TF4_PWR_GPIO_Port GPIOB
#define TF_SEL1_Pin GPIO_PIN_12
#define TF_SEL1_GPIO_Port GPIOA
#define TF_SEL2_Pin GPIO_PIN_13
#define TF_SEL2_GPIO_Port GPIOB

#define W5500_SCLK_Pin GPIO_PIN_8
#define W5500_SCLK_GPIO_Port GPIOA
#define W5500_MISO_Pin GPIO_PIN_9
#define W5500_MISO_GPIO_Port GPIOA
#define W5500_MOSI_Pin GPIO_PIN_10
#define W5500_MOSI_GPIO_Port GPIOA
#define W5500_INT_Pin GPIO_PIN_11
#define W5500_INT_GPIO_Port GPIOA
#define W5500_RESET_Pin GPIO_PIN_6
#define W5500_RESET_GPIO_Port GPIOC
#define W5500_CS_Pin GPIO_PIN_7
#define W5500_CS_GPIO_Port GPIOC

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
