#include "time_service.h"
#include "app_log.h"
#include "stm32u5xx_hal.h"
#include "system_config.h"

static bool s_time_valid;
static uint64_t s_unix_time_ms;
static uint32_t s_source_tick_ms;

void TimeService_Init(void)
{
  s_time_valid = false;
  s_unix_time_ms = 0u;
  s_source_tick_ms = HAL_GetTick();
  APP_LOG_INFO("TimeService init, time invalid");
}

void TimeService_SetUnixTimeMs(uint64_t unix_time_ms)
{
  time_calendar_t calendar;

  s_unix_time_ms = unix_time_ms;
  s_source_tick_ms = HAL_GetTick();
  s_time_valid = true;
  if (TimeService_GetLocalCalendar(&calendar))
  {
    APP_LOG_OK("time synced local=%04u-%02u-%02u %02u:%02u:%02u utc_offset_min=%d",
               (unsigned int)calendar.year,
               (unsigned int)calendar.month,
               (unsigned int)calendar.day,
               (unsigned int)calendar.hour,
               (unsigned int)calendar.minute,
               (unsigned int)calendar.second,
               SYSTEM_LOCAL_UTC_OFFSET_MINUTES);
  }
  else
  {
    APP_LOG_WARN("time sync value cannot convert to local calendar");
  }
}

bool TimeService_IsValid(void)
{
  return s_time_valid;
}

uint64_t TimeService_NowMs(void)
{
  uint32_t elapsed = HAL_GetTick() - s_source_tick_ms;

  if (!s_time_valid)
  {
    return (uint64_t)HAL_GetTick();
  }

  return s_unix_time_ms + elapsed;
}

void TimeService_GetSnapshot(time_snapshot_t *snapshot)
{
  if (snapshot == 0)
  {
    return;
  }

  snapshot->valid = s_time_valid;
  snapshot->unix_time_ms = TimeService_NowMs();
  snapshot->source_tick_ms = s_source_tick_ms;
}

bool TimeService_UnixMsToLocalCalendar(uint64_t unix_time_ms, time_calendar_t *calendar)
{
  uint64_t unix_seconds;
  int64_t local_seconds;
  int64_t days;
  uint32_t seconds_of_day;
  int64_t z;
  int64_t era;
  uint32_t day_of_era;
  uint32_t year_of_era;
  int64_t year;
  uint32_t day_of_year;
  uint32_t month_prime;
  uint32_t month;
  uint32_t day;

  if (calendar == 0)
  {
    return false;
  }

  unix_seconds = unix_time_ms / 1000u;
  local_seconds = (int64_t)unix_seconds + ((int64_t)SYSTEM_LOCAL_UTC_OFFSET_MINUTES * 60);
  if (local_seconds < 0)
  {
    return false;
  }

  days = local_seconds / 86400;
  seconds_of_day = (uint32_t)(local_seconds % 86400);

  /* Convert days since 1970-01-01 to the proleptic Gregorian calendar. */
  z = days + 719468;
  era = (z >= 0) ? (z / 146097) : ((z - 146096) / 146097);
  day_of_era = (uint32_t)(z - (era * 146097));
  year_of_era = (day_of_era - (day_of_era / 1460u) + (day_of_era / 36524u) -
                 (day_of_era / 146096u)) / 365u;
  year = (int64_t)year_of_era + (era * 400);
  day_of_year = day_of_era - ((365u * year_of_era) + (year_of_era / 4u) -
                              (year_of_era / 100u));
  month_prime = ((5u * day_of_year) + 2u) / 153u;
  day = day_of_year - (((153u * month_prime) + 2u) / 5u) + 1u;
  month = (month_prime < 10u) ? (month_prime + 3u) : (month_prime - 9u);
  year += (month <= 2u) ? 1 : 0;

  if ((year < 0) || (year > 65535))
  {
    return false;
  }

  calendar->year = (uint16_t)year;
  calendar->month = (uint8_t)month;
  calendar->day = (uint8_t)day;
  calendar->hour = (uint8_t)(seconds_of_day / 3600u);
  seconds_of_day %= 3600u;
  calendar->minute = (uint8_t)(seconds_of_day / 60u);
  calendar->second = (uint8_t)(seconds_of_day % 60u);
  return true;
}

bool TimeService_GetLocalCalendar(time_calendar_t *calendar)
{
  if (!s_time_valid)
  {
    return false;
  }

  return TimeService_UnixMsToLocalCalendar(TimeService_NowMs(), calendar);
}
