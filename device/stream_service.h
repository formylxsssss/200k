#ifndef __STREAM_SERVICE_H__
#define __STREAM_SERVICE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "system_config.h"

void StreamService_Init(void);
bool StreamService_Start(const acquisition_settings_t *settings);
void StreamService_Stop(void);
void StreamService_Task(void);
bool StreamService_IsActive(void);

#ifdef __cplusplus
}
#endif

#endif /* __STREAM_SERVICE_H__ */
