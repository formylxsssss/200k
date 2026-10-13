#ifndef __TIME_SERVICE_H__
#define __TIME_SERVICE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
  bool valid;
  uint64_t unix_time_ms;
  uint32_t source_tick_ms;
} time_snapshot_t;

typedef struct
{
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
} time_calendar_t;

void TimeService_Init(void);
void TimeService_SetUnixTimeMs(uint64_t unix_time_ms);
bool TimeService_IsValid(void);
uint64_t TimeService_NowMs(void);
void TimeService_GetSnapshot(time_snapshot_t *snapshot);
bool TimeService_UnixMsToLocalCalendar(uint64_t unix_time_ms, time_calendar_t *calendar);
bool TimeService_GetLocalCalendar(time_calendar_t *calendar);

#ifdef __cplusplus
}
#endif

#endif /* __TIME_SERVICE_H__ */
