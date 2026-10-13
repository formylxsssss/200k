#ifndef __APP_CONTROLLER_H__
#define __APP_CONTROLLER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "system_config.h"

void AppController_Init(void);
void AppController_Task(void);
system_mode_t AppController_GetMode(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_CONTROLLER_H__ */
