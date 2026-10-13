#ifndef __APP_LOG_H__
#define __APP_LOG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "SEGGER_RTT.h"

#ifndef APP_LOG_ENABLE
#define APP_LOG_ENABLE 1
#endif

#if APP_LOG_ENABLE
#define APP_LOG_RAW(...)    SEGGER_RTT_printf(0, __VA_ARGS__)
#define APP_LOG_INFO(fmt, ...)  SEGGER_RTT_printf(0, "[INFO] " fmt "\r\n", ##__VA_ARGS__)
#define APP_LOG_OK(fmt, ...)    SEGGER_RTT_printf(0, "[OK] " fmt "\r\n", ##__VA_ARGS__)
#define APP_LOG_WARN(fmt, ...)  SEGGER_RTT_printf(0, "[WARN] " fmt "\r\n", ##__VA_ARGS__)
#define APP_LOG_ERR(fmt, ...)   SEGGER_RTT_printf(0, "[ERR] " fmt "\r\n", ##__VA_ARGS__)
#else
#define APP_LOG_RAW(...)    do {} while (0)
#define APP_LOG_INFO(...)   do {} while (0)
#define APP_LOG_OK(...)     do {} while (0)
#define APP_LOG_WARN(...)   do {} while (0)
#define APP_LOG_ERR(...)    do {} while (0)
#endif

static inline void AppLog_Init(void)
{
#if APP_LOG_ENABLE
  SEGGER_RTT_Init();
#ifdef SEGGER_RTT_MODE_NO_BLOCK_TRIM
  SEGGER_RTT_SetFlagsUpBuffer(0, SEGGER_RTT_MODE_NO_BLOCK_TRIM);
#endif
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* __APP_LOG_H__ */
