/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body.
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "gpio.h"
#include "sdmmc.h"
#include "spi.h"
#include "tim.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "acquisition.h"
#include "app_controller.h"
#include "app_config.h"
#include "app_log.h"
#include "range_ctrl.h"
#include "tf_slots.h"
#include "w5500_ll.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void App_Init(void);
static void DebugLog_Init(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  HAL_Init();
  DebugLog_Init();
  APP_LOG_INFO("HAL_Init ok");

  APP_LOG_INFO("SystemClock_Config start");
  SystemClock_Config();
  APP_LOG_OK("System clock configured source=HSI sysclk=%uHz", (unsigned int)SystemCoreClock);

  MX_GPIO_Init();
  APP_LOG_OK("GPIO init done");
  MX_SPI1_Init();
  APP_LOG_OK("SPI1 init done");
  MX_SDMMC1_SD_Init();
  APP_LOG_OK("SDMMC1 init done");
  MX_SDMMC2_SD_Init();
  APP_LOG_OK("SDMMC2 init done");
  MX_TIM3_Init();
  APP_LOG_OK("TIM3 init done");

  /* USER CODE BEGIN 2 */
  App_Init();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    AppController_Task();
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    APP_LOG_ERR("PWR voltage scaling config failed");
    Error_Handler();
  }

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLMBOOST = RCC_PLLMBOOST_DIV1;
  RCC_OscInitStruct.PLL.PLLN = 76;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLLVCIRANGE_0;
  RCC_OscInitStruct.PLL.PLLFRACN = 6554;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    APP_LOG_ERR("RCC oscillator config failed");
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_PCLK3;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    APP_LOG_ERR("RCC clock config failed");
    Error_Handler();
  }

  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_SDMMC;
  PeriphClkInit.SdmmcClockSelection = RCC_SDMMCCLKSOURCE_PLL1;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    APP_LOG_ERR("SDMMC kernel clock config failed");
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
static void DebugLog_Init(void)
{
  AppLog_Init();
  APP_LOG_RAW("\r\n\r\n");
  APP_LOG_INFO("single_channel_device boot");
}

static void App_Init(void)
{
  const app_net_config_t *net = AppConfig_GetNet();

  APP_LOG_INFO("App_Init start");

  TFSlot_AllOff();
  APP_LOG_OK("TF slots powered off");

  RangeCtrl_SetSwitchBits(APP_DEFAULT_RANGE_SWITCH_BITS);
  APP_LOG_OK("gain control defaulted hardware=0DB software=0DB PB6 selected");

  if (HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3) != HAL_OK)
  {
    APP_LOG_ERR("TIM3 CH3 PWM start failed");
    Error_Handler();
  }
  APP_LOG_OK("ADC clock PWM started on TIM3_CH3");

  APP_LOG_INFO("W5500 reset start");
  W5500_Reset();
  if (W5500_BasicInit(net->mac, net->ip, net->gateway, net->netmask) == HAL_OK)
  {
    APP_LOG_OK("W5500 basic init ok");
  }
  else
  {
    APP_LOG_ERR("W5500 basic init failed");
  }

  Acquisition_Init();
  AppController_Init();

  APP_LOG_OK("App_Init done");
}

/* USER CODE END 4 */

void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  AppLog_Init();
  APP_LOG_ERR("Error_Handler entered");
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  (void)file;
  (void)line;
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
