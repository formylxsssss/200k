#include "ff.h"
#include "time_service.h"

DWORD get_fattime(void)
{
  time_calendar_t calendar;

  if (!TimeService_GetLocalCalendar(&calendar) ||
      (calendar.year < 1980u) || (calendar.year > 2107u))
  {
    return ((DWORD)1u << 21) | ((DWORD)1u << 16);
  }

  return ((DWORD)(calendar.year - 1980u) << 25) |
         ((DWORD)calendar.month << 21) |
         ((DWORD)calendar.day << 16) |
         ((DWORD)calendar.hour << 11) |
         ((DWORD)calendar.minute << 5) |
         ((DWORD)calendar.second >> 1);
}
