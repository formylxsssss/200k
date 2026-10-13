/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    sdmmc.h
  * @brief   This file contains all the function prototypes for sdmmc.c.
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __SDMMC_H__
#define __SDMMC_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

extern SD_HandleTypeDef hsd1;
extern SD_HandleTypeDef hsd2;

void MX_SDMMC1_SD_Init(void);
void MX_SDMMC2_SD_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* __SDMMC_H__ */
