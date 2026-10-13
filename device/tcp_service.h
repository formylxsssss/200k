#ifndef __TCP_SERVICE_H__
#define __TCP_SERVICE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "command_protocol.h"

typedef enum
{
  TCP_SERVICE_DISCONNECTED = 0,
  TCP_SERVICE_LISTENING,
  TCP_SERVICE_CONNECTED,
} tcp_service_state_t;

void TcpService_Init(void);
void TcpService_SetRealtimeConfig(const acquisition_settings_t *settings);
void TcpService_Task(void);
bool TcpService_PollCommand(command_request_t *request);
bool TcpService_SendResponse(const command_response_t *response);
bool TcpService_CanSendRealtimeSamples(size_t count, sample_precision_t precision);
bool TcpService_SendRealtimeSamples(const int32_t *samples, size_t count, sample_precision_t precision);
bool TcpService_IsControlConnected(void);
bool TcpService_IsDataConnected(void);
tcp_service_state_t TcpService_GetState(void);

#ifdef __cplusplus
}
#endif

#endif /* __TCP_SERVICE_H__ */
