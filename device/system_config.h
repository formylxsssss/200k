#ifndef __SYSTEM_CONFIG_H__
#define __SYSTEM_CONFIG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "app_config.h"

#define SYSTEM_SAMPLE_RATE_MIN_HZ       1000u
#define SYSTEM_SAMPLE_RATE_DEFAULT_HZ   200000u
#define SYSTEM_SAMPLE_RATE_MAX_HZ       200000u
#define SYSTEM_REALTIME_RATE_MAX_HZ     96000u
#define SYSTEM_TASK_TABLE_MAX_ITEMS     16u
#define SYSTEM_STORAGE_MAX_CARDS        4u
#define SYSTEM_MAX_CARD_BYTES           2199023255552ULL
#define SYSTEM_MAX_TOTAL_STORAGE_BYTES  (SYSTEM_MAX_CARD_BYTES * SYSTEM_STORAGE_MAX_CARDS)
#define SYSTEM_TCP_CONTROL_PORT         5000u
#define SYSTEM_TCP_DATA_PORT            5001u
#define SYSTEM_TCP_FILE_PORT            5002u
#define SYSTEM_LOCAL_UTC_OFFSET_MINUTES 480
#define SYSTEM_TRIGGER_DURATION_DEFAULT_MS 10000ULL
#define SYSTEM_TRIGGER_DURATION_MAX_MS     86400000ULL

typedef enum
{
  SYSTEM_MODE_IDLE = 0,
  SYSTEM_MODE_REALTIME_STREAM,
  SYSTEM_MODE_STORAGE_IMMEDIATE,
  SYSTEM_MODE_STORAGE_SCHEDULED,
  SYSTEM_MODE_TRIGGER_ARMED,
  SYSTEM_MODE_REALTIME_TRIGGERED,
  SYSTEM_MODE_STORAGE_TRIGGERED,
  SYSTEM_MODE_FILE_SERVICE,
  SYSTEM_MODE_ERROR,
} system_mode_t;

typedef enum
{
  TRIGGER_OUTPUT_STORAGE = 0,
  TRIGGER_OUTPUT_REALTIME,
} trigger_output_t;

typedef enum
{
  SAMPLE_PRECISION_16BIT = 16,
  SAMPLE_PRECISION_24BIT = 24,
  SAMPLE_PRECISION_32BIT = 32,
} sample_precision_t;

typedef enum
{
  STORAGE_POLICY_FIRST_AVAILABLE = 0,
  STORAGE_POLICY_FILL_CURRENT_THEN_NEXT,
  STORAGE_POLICY_BALANCE_FREE_SPACE,
} storage_policy_t;

typedef struct
{
  uint32_t sample_rate_hz;
  sample_precision_t precision;
  /* Software-only attenuation. PC4/PC5 are not used by acquisition. */
  app_attenuation_t attenuation;
  uint8_t range_switch_bits;
  storage_policy_t storage_policy;
} acquisition_settings_t;

typedef struct
{
  uint32_t task_id;
  bool enabled;
  uint64_t start_time_ms;
  uint64_t interval_ms;
  uint64_t duration_ms;
  uint32_t repeat_count;
  acquisition_settings_t settings;
} acquisition_task_item_t;

const acquisition_settings_t *SystemConfig_GetDefaultAcquisition(void);
bool SystemConfig_IsSupportedSampleRate(uint32_t sample_rate_hz);
bool SystemConfig_ValidateAcquisition(const acquisition_settings_t *settings);
bool SystemConfig_ValidateRealtimeAcquisition(const acquisition_settings_t *settings);
bool SystemConfig_ApplyGainDb(int32_t gain_db, acquisition_settings_t *settings);
bool SystemConfig_GetNominalGainDb(const acquisition_settings_t *settings, int32_t *gain_db);
bool SystemConfig_GetHardwareGainDb(const acquisition_settings_t *settings, int32_t *gain_db);
int32_t SystemConfig_GetSoftwareGainDb(const acquisition_settings_t *settings);
uint32_t SystemConfig_GetSoftwareGainQ30(const acquisition_settings_t *settings);
const char *SystemMode_ToString(system_mode_t mode);
const char *SamplePrecision_ToString(sample_precision_t precision);

#ifdef __cplusplus
}
#endif

#endif /* __SYSTEM_CONFIG_H__ */
