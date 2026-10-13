#ifndef __STM32U5xx_HAL_CONF_H
#define __STM32U5xx_HAL_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_EXTI_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_SD_MODULE_ENABLED
#define HAL_SPI_MODULE_ENABLED
#define HAL_TIM_MODULE_ENABLED

#define HSE_VALUE 16000000U
#define HSE_STARTUP_TIMEOUT 100UL
#define LSE_VALUE 32768U
#define LSE_STARTUP_TIMEOUT 5000UL
#define HSI_VALUE 16000000U
#define MSI_VALUE 4000000U
#define LSI_VALUE 32000U
#define HSI48_VALUE 48000000U
#define EXTERNAL_CLOCK_VALUE 12288000U
#define EXTERNAL_SAI1_CLOCK_VALUE 48000UL
#define VDD_VALUE 3300U
#define TICK_INT_PRIORITY 0U
#define USE_RTOS 0U
#define PREFETCH_ENABLE 1U
#define INSTRUCTION_CACHE_ENABLE 1U
#define DATA_CACHE_ENABLE 1U

#include "stm32u5xx_hal_rcc.h"
#include "stm32u5xx_hal_gpio.h"
#include "stm32u5xx_hal_dma.h"
#include "stm32u5xx_hal_cortex.h"
#include "stm32u5xx_hal_exti.h"
#include "stm32u5xx_hal_flash.h"
#include "stm32u5xx_hal_pwr.h"
#include "stm32u5xx_hal_sd.h"
#include "stm32u5xx_hal_spi.h"
#include "stm32u5xx_hal_tim.h"

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line);
#else
#define assert_param(expr) ((void)0U)
#endif

#ifdef __cplusplus
}
#endif

#endif /* __STM32U5xx_HAL_CONF_H */
