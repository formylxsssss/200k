#include "ads127l11.h"
#include "app_config.h"
#include "app_log.h"
#include "main.h"

#define ADS127L11_CMD_RREG_BASE       0x40u
#define ADS127L11_CMD_WREG_BASE       0x80u
#define ADS127L11_REG_MUX             0x04u
#define ADS127L11_REG_CONFIG1         0x05u
#define ADS127L11_REG_CONFIG2         0x06u
#define ADS127L11_REG_CONFIG3         0x07u
#define ADS127L11_REG_CONFIG4         0x08u

#define ADS127L11_MUX_NORMAL          0x00u
#define ADS127L11_CONFIG1_REF_HIGH    0x40u
#define ADS127L11_CONFIG1_VCM_ENABLE  0x10u
#define ADS127L11_CONFIG1_REFP_BUF    0x08u
#define ADS127L11_CONFIG1_AINP_BUF    0x02u
#define ADS127L11_CONFIG1_AINN_BUF    0x01u
#define ADS127L11_CONFIG1_DEFAULT     (ADS127L11_CONFIG1_REF_HIGH | \
                                       ADS127L11_CONFIG1_VCM_ENABLE | \
                                       ADS127L11_CONFIG1_REFP_BUF | \
                                       ADS127L11_CONFIG1_AINP_BUF | \
                                       ADS127L11_CONFIG1_AINN_BUF)
#define ADS127L11_CONFIG2_DEFAULT     0x00u
#define ADS127L11_CONFIG3_DEFAULT     0x00u
#define ADS127L11_CONFIG4_CLK_EXT     0x80u
#define ADS127L11_CONFIG4_DATA_16BIT  0x08u
#define ADS127L11_FAST_SPI_GUARD      512u

#if defined(__GNUC__)
#define ADS127L11_ALWAYS_INLINE static inline __attribute__((always_inline))
#define ADS127L11_FAST_CODE __attribute__((hot, optimize("O3")))
#else
#define ADS127L11_ALWAYS_INLINE static inline
#define ADS127L11_FAST_CODE
#endif

static HAL_StatusTypeDef ADS127L11_Transfer(ads127l11_t *dev, const uint8_t *tx, uint8_t *rx, uint16_t len)
{
  HAL_StatusTypeDef status;

  HAL_GPIO_WritePin(ADS_CS_GPIO_Port, ADS_CS_Pin, GPIO_PIN_RESET);
  status = HAL_SPI_TransmitReceive(dev->spi, (uint8_t *)tx, rx, len, APP_ADC_SPI_TIMEOUT_MS);
  HAL_GPIO_WritePin(ADS_CS_GPIO_Port, ADS_CS_Pin, GPIO_PIN_SET);

  return status;
}

static HAL_StatusTypeDef ADS127L11_ReadRegister(ads127l11_t *dev, uint8_t address, uint8_t *value)
{
  uint8_t tx_cmd[2];
  uint8_t rx_cmd[2] = {0u, 0u};
  uint8_t tx_resp[2] = {0u, 0u};
  uint8_t rx_resp[2] = {0u, 0u};
  HAL_StatusTypeDef status;

  tx_cmd[0] = (uint8_t)(ADS127L11_CMD_RREG_BASE | (address & 0x0Fu));
  tx_cmd[1] = 0x00u;

  status = ADS127L11_Transfer(dev, tx_cmd, rx_cmd, sizeof(tx_cmd));
  if (status != HAL_OK)
  {
    return status;
  }

  status = ADS127L11_Transfer(dev, tx_resp, rx_resp, sizeof(tx_resp));
  if (status == HAL_OK)
  {
    *value = rx_resp[0];
  }

  return status;
}

static HAL_StatusTypeDef ADS127L11_WriteRegister(ads127l11_t *dev, uint8_t address, uint8_t value)
{
  uint8_t tx[2];
  uint8_t rx[2] = {0u, 0u};

  tx[0] = (uint8_t)(ADS127L11_CMD_WREG_BASE | (address & 0x0Fu));
  tx[1] = value;
  return ADS127L11_Transfer(dev, tx, rx, sizeof(tx));
}

static uint8_t ADS127L11_Config4ForPrecision(sample_precision_t precision)
{
  uint8_t config4 = ADS127L11_CONFIG4_CLK_EXT;

  if (precision == SAMPLE_PRECISION_16BIT)
  {
    config4 |= ADS127L11_CONFIG4_DATA_16BIT;
  }

  return config4;
}

static const char *ADS127L11_AdcPrecisionString(sample_precision_t precision)
{
  return (precision == SAMPLE_PRECISION_16BIT) ? "16BIT" : "24BIT";
}

static uint8_t ADS127L11_SampleBytesForPrecision(sample_precision_t precision)
{
  return (precision == SAMPLE_PRECISION_16BIT) ? 2u : 3u;
}

static void ADS127L11_DecodeSample(const uint8_t *rx, uint8_t sample_bytes, int32_t *sample)
{
  uint32_t raw;

  if (sample_bytes == 2u)
  {
    *sample = ((int32_t)(int16_t)(((uint16_t)rx[0] << 8) | rx[1])) << 8;
    return;
  }

  raw = ((uint32_t)rx[0] << 16) | ((uint32_t)rx[1] << 8) | rx[2];
  if ((raw & 0x00800000u) != 0u)
  {
    raw |= 0xFF000000u;
  }
  *sample = (int32_t)raw;
}

ADS127L11_ALWAYS_INLINE bool ADS127L11_WaitSpiFlag(SPI_TypeDef *spi, uint32_t flag)
{
  uint32_t guard = ADS127L11_FAST_SPI_GUARD;

  while ((spi->SR & flag) == 0u)
  {
    if (--guard == 0u)
    {
      return false;
    }
  }
  return true;
}

ADS127L11_ALWAYS_INLINE void ADS127L11_CloseFastTransfer(SPI_TypeDef *spi)
{
  ADS_CS_GPIO_Port->BSRR = ADS_CS_Pin;
  CLEAR_BIT(spi->CR1, SPI_CR1_SPE);
  spi->IFCR = SPI_IFCR_EOTC | SPI_IFCR_TXTFC | SPI_IFCR_OVRC;
}

static HAL_StatusTypeDef ADS127L11_VerifyConfig(ads127l11_t *dev,
                                                uint8_t expected_config4)
{
  uint8_t mux = 0u;
  uint8_t config1 = 0u;
  uint8_t config2 = 0u;
  uint8_t config3 = 0u;
  uint8_t config4 = 0u;

  if ((ADS127L11_ReadRegister(dev, ADS127L11_REG_MUX, &mux) != HAL_OK) ||
      (ADS127L11_ReadRegister(dev, ADS127L11_REG_CONFIG1, &config1) != HAL_OK) ||
      (ADS127L11_ReadRegister(dev, ADS127L11_REG_CONFIG2, &config2) != HAL_OK) ||
      (ADS127L11_ReadRegister(dev, ADS127L11_REG_CONFIG3, &config3) != HAL_OK) ||
      (ADS127L11_ReadRegister(dev, ADS127L11_REG_CONFIG4, &config4) != HAL_OK))
  {
    APP_LOG_ERR("ADS127L11 register readback failed");
    return HAL_ERROR;
  }

  APP_LOG_INFO("ADS127L11 regs mux=0x%02X cfg1=0x%02X cfg2=0x%02X cfg3=0x%02X cfg4=0x%02X",
               (unsigned int)mux,
               (unsigned int)config1,
               (unsigned int)config2,
               (unsigned int)config3,
               (unsigned int)config4);

  if ((mux != ADS127L11_MUX_NORMAL) ||
      (config1 != ADS127L11_CONFIG1_DEFAULT) ||
      (config2 != ADS127L11_CONFIG2_DEFAULT) ||
      (config3 != ADS127L11_CONFIG3_DEFAULT) ||
      (config4 != expected_config4))
  {
    APP_LOG_ERR("ADS127L11 verify expected mux=0x%02X cfg1=0x%02X cfg2=0x%02X cfg3=0x%02X cfg4=0x%02X",
                (unsigned int)ADS127L11_MUX_NORMAL,
                (unsigned int)ADS127L11_CONFIG1_DEFAULT,
                (unsigned int)ADS127L11_CONFIG2_DEFAULT,
                (unsigned int)ADS127L11_CONFIG3_DEFAULT,
                (unsigned int)expected_config4);
    APP_LOG_ERR("ADS127L11 register verify mismatch");
    return HAL_ERROR;
  }

  APP_LOG_OK("ADS127L11 register verify ok");
  return HAL_OK;
}

static HAL_StatusTypeDef ADS127L11_ApplyConfig(ads127l11_t *dev,
                                               sample_precision_t precision)
{
  uint8_t config4 = ADS127L11_Config4ForPrecision(precision);

  if (ADS127L11_WriteRegister(dev, ADS127L11_REG_MUX, ADS127L11_MUX_NORMAL) != HAL_OK)
  {
    APP_LOG_ERR("ADS127L11 MUX config failed");
    return HAL_ERROR;
  }
  if (ADS127L11_WriteRegister(dev, ADS127L11_REG_CONFIG1, ADS127L11_CONFIG1_DEFAULT) != HAL_OK)
  {
    APP_LOG_ERR("ADS127L11 CONFIG1 config failed");
    return HAL_ERROR;
  }
  if (ADS127L11_WriteRegister(dev, ADS127L11_REG_CONFIG2, ADS127L11_CONFIG2_DEFAULT) != HAL_OK)
  {
    APP_LOG_ERR("ADS127L11 CONFIG2 config failed");
    return HAL_ERROR;
  }
  if (ADS127L11_WriteRegister(dev, ADS127L11_REG_CONFIG3, ADS127L11_CONFIG3_DEFAULT) != HAL_OK)
  {
    APP_LOG_ERR("ADS127L11 CONFIG3 config failed");
    return HAL_ERROR;
  }
  if (ADS127L11_WriteRegister(dev, ADS127L11_REG_CONFIG4, config4) != HAL_OK)
  {
    APP_LOG_ERR("ADS127L11 CONFIG4 config failed");
    return HAL_ERROR;
  }
  if (ADS127L11_VerifyConfig(dev, config4) != HAL_OK)
  {
    return HAL_ERROR;
  }

  dev->sample_bytes = ADS127L11_SampleBytesForPrecision(precision);
  return HAL_OK;
}

HAL_StatusTypeDef ADS127L11_Init(ads127l11_t *dev, SPI_HandleTypeDef *spi)
{
  HAL_StatusTypeDef status;

  dev->spi = spi;
  dev->sample_bytes = 3u;
  HAL_GPIO_WritePin(ADS_CS_GPIO_Port, ADS_CS_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(ADS_SYNC_GPIO_Port, ADS_SYNC_Pin, GPIO_PIN_SET);
  APP_LOG_INFO("ADS127L11 init");

  status = ADS127L11_HardwareReset(dev);
  if (status != HAL_OK)
  {
    APP_LOG_ERR("ADS127L11 hardware reset failed");
    return status;
  }

  return ADS127L11_ConfigureDefault(dev);
}

HAL_StatusTypeDef ADS127L11_HardwareReset(ads127l11_t *dev)
{
  (void)dev;

  APP_LOG_INFO("ADS127L11 hardware reset");
  HAL_GPIO_WritePin(ADS_RESET_GPIO_Port, ADS_RESET_Pin, GPIO_PIN_RESET);
  HAL_Delay(2);
  HAL_GPIO_WritePin(ADS_RESET_GPIO_Port, ADS_RESET_Pin, GPIO_PIN_SET);
  HAL_Delay(5);

  APP_LOG_OK("ADS127L11 reset done");
  return HAL_OK;
}

HAL_StatusTypeDef ADS127L11_ConfigureDefault(ads127l11_t *dev)
{
  HAL_StatusTypeDef status;

  status = ADS127L11_ApplyConfig(dev, SAMPLE_PRECISION_24BIT);
  if (status != HAL_OK)
  {
    APP_LOG_ERR("ADS127L11 default config failed");
    return status;
  }

  ADS127L11_SyncPulse();
  APP_LOG_OK("ADS127L11 default config applied adc_data=24BIT vcm=on ref=high filter=wideband_osr32");
  return HAL_OK;
}

HAL_StatusTypeDef ADS127L11_ConfigureAcquisition(ads127l11_t *dev, const acquisition_settings_t *settings)
{
  HAL_StatusTypeDef status;

  if ((dev == 0) || (settings == 0))
  {
    return HAL_ERROR;
  }

  status = ADS127L11_ApplyConfig(dev, settings->precision);
  if (status != HAL_OK)
  {
    APP_LOG_ERR("ADS127L11 apply acquisition config failed");
    return status;
  }

  ADS127L11_SyncPulse();
  APP_LOG_OK("ADS127L11 config rate=%u precision=%s sample_bytes=%u",
             (unsigned int)settings->sample_rate_hz,
             ADS127L11_AdcPrecisionString(settings->precision),
             (unsigned int)dev->sample_bytes);
  if (settings->precision == SAMPLE_PRECISION_32BIT)
  {
    APP_LOG_INFO("ADS127L11 request precision=32BIT, adc_data=24BIT, tcp_payload=32BIT sign-extended");
  }
  return HAL_OK;
}

HAL_StatusTypeDef ADS127L11_ReadSample(ads127l11_t *dev, int32_t *sample)
{
  uint8_t tx[3] = {0x00, 0x00, 0x00};
  uint8_t rx[3] = {0x00, 0x00, 0x00};
  HAL_StatusTypeDef status;

  status = ADS127L11_Transfer(dev, tx, rx, dev->sample_bytes);
  if (status != HAL_OK)
  {
    return status;
  }

  ADS127L11_DecodeSample(rx, dev->sample_bytes, sample);

  return HAL_OK;
}

ADS127L11_FAST_CODE HAL_StatusTypeDef ADS127L11_ReadSampleFast(ads127l11_t *dev, int32_t *sample)
{
  SPI_HandleTypeDef *spi;
  SPI_TypeDef *instance;
  uint8_t rx[3] = {0u, 0u, 0u};
  uint8_t sample_bytes;

  if ((dev == 0) || (dev->spi == 0) || (sample == 0) ||
      ((dev->sample_bytes != 2u) && (dev->sample_bytes != 3u)))
  {
    return HAL_ERROR;
  }

  spi = dev->spi;
  instance = spi->Instance;
  sample_bytes = dev->sample_bytes;
  if (spi->State != HAL_SPI_STATE_READY)
  {
    return HAL_BUSY;
  }

  CLEAR_BIT(instance->CR1, SPI_CR1_SPE);
  instance->IFCR = SPI_IFCR_EOTC | SPI_IFCR_TXTFC | SPI_IFCR_OVRC;
  MODIFY_REG(instance->CR2, SPI_CR2_TSIZE, sample_bytes);
  ADS_CS_GPIO_Port->BSRR = ((uint32_t)ADS_CS_Pin << 16u);
  SET_BIT(instance->CR1, SPI_CR1_SPE);

  /* The FIFO is empty here. Queue the complete frame before starting SCLK so
     the ISR does not wait for each individual byte. */
  if (!ADS127L11_WaitSpiFlag(instance, SPI_FLAG_TXP))
  {
    ADS127L11_CloseFastTransfer(instance);
    return HAL_TIMEOUT;
  }
  *((__IO uint8_t *)&instance->TXDR) = 0u;
  *((__IO uint8_t *)&instance->TXDR) = 0u;
  if (sample_bytes == 3u)
  {
    *((__IO uint8_t *)&instance->TXDR) = 0u;
  }
  SET_BIT(instance->CR1, SPI_CR1_CSTART);

  if (!ADS127L11_WaitSpiFlag(instance, SPI_FLAG_EOT))
  {
    ADS127L11_CloseFastTransfer(instance);
    return HAL_TIMEOUT;
  }

  rx[0] = *((__IO uint8_t *)&instance->RXDR);
  rx[1] = *((__IO uint8_t *)&instance->RXDR);
  if (sample_bytes == 3u)
  {
    rx[2] = *((__IO uint8_t *)&instance->RXDR);
  }

  ADS127L11_CloseFastTransfer(instance);
  ADS127L11_DecodeSample(rx, sample_bytes, sample);
  return HAL_OK;
}

void ADS127L11_SyncPulse(void)
{
  HAL_GPIO_WritePin(ADS_SYNC_GPIO_Port, ADS_SYNC_Pin, GPIO_PIN_SET);
  HAL_Delay(1);
  HAL_GPIO_WritePin(ADS_SYNC_GPIO_Port, ADS_SYNC_Pin, GPIO_PIN_RESET);
  HAL_Delay(1);
  HAL_GPIO_WritePin(ADS_SYNC_GPIO_Port, ADS_SYNC_Pin, GPIO_PIN_SET);
}

bool ADS127L11_DataReadyLevel(void)
{
  return HAL_GPIO_ReadPin(ADS_DRDY_GPIO_Port, ADS_DRDY_Pin) == GPIO_PIN_RESET;
}
