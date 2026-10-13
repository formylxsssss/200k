#include "system_config.h"

typedef struct
{
  int32_t gain_db;
  app_attenuation_t attenuation;
  uint8_t range_switch_bits;
} gain_profile_t;

static const acquisition_settings_t s_default_acquisition =
{
  .sample_rate_hz = SYSTEM_SAMPLE_RATE_DEFAULT_HZ,
  .precision = SAMPLE_PRECISION_24BIT,
  .attenuation = APP_DEFAULT_ATTENUATION,
  .range_switch_bits = APP_DEFAULT_RANGE_SWITCH_BITS,
  .storage_policy = STORAGE_POLICY_FIRST_AVAILABLE,
};

static const gain_profile_t s_gain_profiles[] =
{
  { -30, APP_ATTENUATION_MINUS_30DB, APP_RANGE_SWITCH_D0 },
  { -20, APP_ATTENUATION_0DB,        APP_RANGE_SWITCH_D3 },
  { -10, APP_ATTENUATION_MINUS_10DB, APP_RANGE_SWITCH_D0 },
  {   0, APP_ATTENUATION_0DB,        APP_RANGE_SWITCH_D0 },
  {  20, APP_ATTENUATION_0DB,        APP_RANGE_SWITCH_D1 },
  {  40, APP_ATTENUATION_0DB,        APP_RANGE_SWITCH_D2 },
};

#define SOFTWARE_GAIN_Q30_0DB       1073741824u
#define SOFTWARE_GAIN_Q30_MINUS_10DB 339546978u
#define SOFTWARE_GAIN_Q30_MINUS_30DB  33954698u

static const uint32_t s_supported_sample_rates[] =
{
  1000u,
  2000u,
  4000u,
  8000u,
  12000u,
  16000u,
  24000u,
  48000u,
  96000u,
  128000u,
  200000u,
};

const acquisition_settings_t *SystemConfig_GetDefaultAcquisition(void)
{
  return &s_default_acquisition;
}

bool SystemConfig_IsSupportedSampleRate(uint32_t sample_rate_hz)
{
  for (uint32_t i = 0u;
       i < (sizeof(s_supported_sample_rates) / sizeof(s_supported_sample_rates[0]));
       ++i)
  {
    if (s_supported_sample_rates[i] == sample_rate_hz)
    {
      return true;
    }
  }

  return false;
}

bool SystemConfig_ValidateAcquisition(const acquisition_settings_t *settings)
{
  if (settings == 0)
  {
    return false;
  }

  if (!SystemConfig_IsSupportedSampleRate(settings->sample_rate_hz))
  {
    return false;
  }

  if ((settings->precision != SAMPLE_PRECISION_16BIT) &&
      (settings->precision != SAMPLE_PRECISION_24BIT) &&
      (settings->precision != SAMPLE_PRECISION_32BIT))
  {
    return false;
  }

  if (settings->attenuation > APP_ATTENUATION_MINUS_30DB)
  {
    return false;
  }

  if ((settings->range_switch_bits & 0xF0u) != 0u)
  {
    return false;
  }

  {
    int32_t gain_db;
    if (!SystemConfig_GetNominalGainDb(settings, &gain_db))
    {
      return false;
    }
  }

  return true;
}

bool SystemConfig_ValidateRealtimeAcquisition(const acquisition_settings_t *settings)
{
  return SystemConfig_ValidateAcquisition(settings) &&
         (settings->sample_rate_hz <= SYSTEM_REALTIME_RATE_MAX_HZ);
}

bool SystemConfig_ApplyGainDb(int32_t gain_db, acquisition_settings_t *settings)
{
  if (settings == 0)
  {
    return false;
  }

  for (uint32_t i = 0u; i < (sizeof(s_gain_profiles) / sizeof(s_gain_profiles[0])); ++i)
  {
    if (s_gain_profiles[i].gain_db == gain_db)
    {
      settings->attenuation = s_gain_profiles[i].attenuation;
      settings->range_switch_bits = s_gain_profiles[i].range_switch_bits;
      return true;
    }
  }

  return false;
}

bool SystemConfig_GetNominalGainDb(const acquisition_settings_t *settings, int32_t *gain_db)
{
  if ((settings == 0) || (gain_db == 0))
  {
    return false;
  }

  for (uint32_t i = 0u; i < (sizeof(s_gain_profiles) / sizeof(s_gain_profiles[0])); ++i)
  {
    if ((s_gain_profiles[i].attenuation == settings->attenuation) &&
        (s_gain_profiles[i].range_switch_bits == (settings->range_switch_bits & 0x0Fu)))
    {
      *gain_db = s_gain_profiles[i].gain_db;
      return true;
    }
  }

  return false;
}

bool SystemConfig_GetHardwareGainDb(const acquisition_settings_t *settings, int32_t *gain_db)
{
  if ((settings == 0) || (gain_db == 0))
  {
    return false;
  }

  switch (settings->range_switch_bits & 0x0Fu)
  {
    case APP_RANGE_SWITCH_D0:
      *gain_db = 0;
      return true;
    case APP_RANGE_SWITCH_D1:
      *gain_db = 20;
      return true;
    case APP_RANGE_SWITCH_D2:
      *gain_db = 40;
      return true;
    case APP_RANGE_SWITCH_D3:
      *gain_db = -20;
      return true;
    default:
      return false;
  }
}

int32_t SystemConfig_GetSoftwareGainDb(const acquisition_settings_t *settings)
{
  if (settings == 0)
  {
    return 0;
  }

  switch (settings->attenuation)
  {
    case APP_ATTENUATION_MINUS_10DB:
      return -10;
    case APP_ATTENUATION_MINUS_30DB:
      return -30;
    case APP_ATTENUATION_0DB:
    default:
      return 0;
  }
}

uint32_t SystemConfig_GetSoftwareGainQ30(const acquisition_settings_t *settings)
{
  if (settings == 0)
  {
    return SOFTWARE_GAIN_Q30_0DB;
  }

  switch (settings->attenuation)
  {
    case APP_ATTENUATION_MINUS_10DB:
      return SOFTWARE_GAIN_Q30_MINUS_10DB;
    case APP_ATTENUATION_MINUS_30DB:
      return SOFTWARE_GAIN_Q30_MINUS_30DB;
    case APP_ATTENUATION_0DB:
    default:
      return SOFTWARE_GAIN_Q30_0DB;
  }
}

const char *SystemMode_ToString(system_mode_t mode)
{
  switch (mode)
  {
    case SYSTEM_MODE_IDLE:
      return "IDLE";
    case SYSTEM_MODE_REALTIME_STREAM:
      return "REALTIME_STREAM";
    case SYSTEM_MODE_STORAGE_IMMEDIATE:
      return "STORAGE_IMMEDIATE";
    case SYSTEM_MODE_STORAGE_SCHEDULED:
      return "STORAGE_SCHEDULED";
    case SYSTEM_MODE_TRIGGER_ARMED:
      return "TRIGGER_ARMED";
    case SYSTEM_MODE_REALTIME_TRIGGERED:
      return "REALTIME_TRIGGERED";
    case SYSTEM_MODE_STORAGE_TRIGGERED:
      return "STORAGE_TRIGGERED";
    case SYSTEM_MODE_FILE_SERVICE:
      return "FILE_SERVICE";
    case SYSTEM_MODE_ERROR:
      return "ERROR";
    default:
      return "UNKNOWN";
  }
}

const char *SamplePrecision_ToString(sample_precision_t precision)
{
  switch (precision)
  {
    case SAMPLE_PRECISION_16BIT:
      return "16BIT";
    case SAMPLE_PRECISION_24BIT:
      return "24BIT";
    case SAMPLE_PRECISION_32BIT:
      return "32BIT";
    default:
      return "UNKNOWN";
  }
}
