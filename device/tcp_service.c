#include "tcp_service.h"
#include "app_log.h"
#include "system_config.h"
#include "w5500_ll.h"
#include <string.h>

#define TCP_SERVICE_RX_BUFFER_SIZE 128u
#define TCP_REALTIME_MAX_SAMPLES   256u
#define TCP_REALTIME_HEADER_SIZE   16u

static void TcpService_WriteLe16(uint8_t *buffer, uint16_t value)
{
  buffer[0] = (uint8_t)(value & 0xFFu);
  buffer[1] = (uint8_t)(value >> 8);
}

static void TcpService_WriteLe32(uint8_t *buffer, uint32_t value)
{
  buffer[0] = (uint8_t)(value & 0xFFu);
  buffer[1] = (uint8_t)((value >> 8) & 0xFFu);
  buffer[2] = (uint8_t)((value >> 16) & 0xFFu);
  buffer[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static size_t TcpService_PackSample(uint8_t *buffer, int32_t sample, sample_precision_t precision)
{
  uint32_t raw = (uint32_t)sample;

  switch (precision)
  {
    case SAMPLE_PRECISION_16BIT:
      raw = (uint32_t)((int16_t)(sample >> 8));
      TcpService_WriteLe16(buffer, (uint16_t)(raw & 0xFFFFu));
      return 2u;

    case SAMPLE_PRECISION_32BIT:
      TcpService_WriteLe32(buffer, raw);
      return 4u;

    case SAMPLE_PRECISION_24BIT:
    default:
      buffer[0] = (uint8_t)(raw & 0xFFu);
      buffer[1] = (uint8_t)((raw >> 8) & 0xFFu);
      buffer[2] = (uint8_t)((raw >> 16) & 0xFFu);
      return 3u;
  }
}

static size_t TcpService_BytesPerSample(sample_precision_t precision)
{
  switch (precision)
  {
    case SAMPLE_PRECISION_16BIT:
      return 2u;
    case SAMPLE_PRECISION_32BIT:
      return 4u;
    case SAMPLE_PRECISION_24BIT:
    default:
      return 3u;
  }
}

static tcp_service_state_t s_state;
static uint8_t s_rx_buffer[TCP_SERVICE_RX_BUFFER_SIZE];
static size_t s_rx_len;
static uint8_t s_last_control_sr;
static uint8_t s_last_data_sr;
static uint32_t s_stream_sequence;
static uint32_t s_stream_send_failures;
static uint32_t s_realtime_sample_rate_hz;

static void TcpService_LogSocketChange(const char *name, uint8_t status, uint8_t *last_status)
{
  if (*last_status != status)
  {
    const char *status_text = "UNKNOWN";

    switch (status)
    {
      case W5500_SOCK_SR_CLOSED:
        status_text = "CLOSED";
        break;
      case W5500_SOCK_SR_INIT:
        status_text = "INIT";
        break;
      case W5500_SOCK_SR_LISTEN:
        status_text = "LISTEN";
        break;
      case W5500_SOCK_SR_SYNSENT:
        status_text = "SYNSENT";
        break;
      case W5500_SOCK_SR_SYNRECV:
        status_text = "SYNRECV";
        break;
      case W5500_SOCK_SR_ESTABLISHED:
        status_text = "ESTABLISHED";
        break;
      case W5500_SOCK_SR_FIN_WAIT:
        status_text = "FIN_WAIT";
        break;
      case W5500_SOCK_SR_CLOSING:
        status_text = "CLOSING";
        break;
      case W5500_SOCK_SR_TIME_WAIT:
        status_text = "TIME_WAIT";
        break;
      case W5500_SOCK_SR_CLOSE_WAIT:
        status_text = "CLOSE_WAIT";
        break;
      case W5500_SOCK_SR_LAST_ACK:
        status_text = "LAST_ACK";
        break;
      default:
        break;
    }

    APP_LOG_INFO("TCP %s socket %s(0x%02X)", name, status_text, (unsigned int)status);
    *last_status = status;
  }
}

static bool TcpService_RxHasLine(void)
{
  for (size_t i = 0; i < s_rx_len; ++i)
  {
    if ((s_rx_buffer[i] == (uint8_t)'\n') || (s_rx_buffer[i] == (uint8_t)'\r'))
    {
      return true;
    }
  }

  return s_rx_len >= sizeof(s_rx_buffer);
}

void TcpService_Init(void)
{
  s_state = TCP_SERVICE_LISTENING;
  s_rx_len = 0u;
  s_last_control_sr = 0xFFu;
  s_last_data_sr = 0xFFu;
  s_stream_sequence = 0u;
  s_stream_send_failures = 0u;
  s_realtime_sample_rate_hz = SystemConfig_GetDefaultAcquisition()->sample_rate_hz;

  (void)W5500_SocketOpenTcpServer(W5500_SOCKET_CONTROL, SYSTEM_TCP_CONTROL_PORT, 2u, 2u);
  (void)W5500_SocketOpenTcpServer(W5500_SOCKET_DATA, SYSTEM_TCP_DATA_PORT, 8u, 1u);
  (void)W5500_SocketOpenTcpServer(W5500_SOCKET_FILE, SYSTEM_TCP_FILE_PORT, 2u, 2u);

  APP_LOG_INFO("TcpService init control=%u data=%u file=%u",
               (unsigned int)SYSTEM_TCP_CONTROL_PORT,
               (unsigned int)SYSTEM_TCP_DATA_PORT,
               (unsigned int)SYSTEM_TCP_FILE_PORT);
}

void TcpService_SetRealtimeConfig(const acquisition_settings_t *settings)
{
  if (settings == NULL)
  {
    return;
  }

  /* Keep packet sequence monotonic across continuous-trigger captures while
     the TCP service remains alive. This lets the host distinguish real gaps
     from an intentional acquisition restart. */
  s_stream_send_failures = 0u;
  s_realtime_sample_rate_hz = settings->sample_rate_hz;
}

void TcpService_Task(void)
{
  uint8_t status;
  uint16_t rx_size;
  size_t room;
  size_t received;

  (void)W5500_SocketServiceTcpServer(W5500_SOCKET_CONTROL, SYSTEM_TCP_CONTROL_PORT);
  (void)W5500_SocketServiceTcpServer(W5500_SOCKET_DATA, SYSTEM_TCP_DATA_PORT);
  (void)W5500_SocketServiceTcpServer(W5500_SOCKET_FILE, SYSTEM_TCP_FILE_PORT);

  status = W5500_SocketStatus(W5500_SOCKET_CONTROL);
  TcpService_LogSocketChange("control", status, &s_last_control_sr);
  status = W5500_SocketStatus(W5500_SOCKET_DATA);
  TcpService_LogSocketChange("data", status, &s_last_data_sr);

  if (TcpService_IsControlConnected())
  {
    rx_size = W5500_SocketRxSize(W5500_SOCKET_CONTROL);
    if (rx_size > 0u)
    {
      room = sizeof(s_rx_buffer) - s_rx_len;
      if (room == 0u)
      {
        s_rx_len = 0u;
        room = sizeof(s_rx_buffer);
      }

      received = W5500_SocketRecv(W5500_SOCKET_CONTROL, &s_rx_buffer[s_rx_len], room);
      s_rx_len += received;
      if (received > 0u)
      {
        APP_LOG_INFO("TCP command rx bytes=%u", (unsigned int)received);
      }
    }
  }

  s_state = TcpService_IsControlConnected() ? TCP_SERVICE_CONNECTED : TCP_SERVICE_LISTENING;
}

bool TcpService_PollCommand(command_request_t *request)
{
  size_t line_end = 0u;
  size_t consume_len;
  size_t command_len;
  bool has_line = false;

  if ((request == 0) || (s_rx_len == 0u))
  {
    return false;
  }
  if (!TcpService_RxHasLine())
  {
    return false;
  }

  for (size_t i = 0u; i < s_rx_len; ++i)
  {
    if ((s_rx_buffer[i] == (uint8_t)'\n') || (s_rx_buffer[i] == (uint8_t)'\r'))
    {
      line_end = i;
      has_line = true;
      break;
    }
  }

  if (!has_line)
  {
    line_end = s_rx_len;
  }

  command_len = line_end;
  consume_len = line_end;
  while ((consume_len < s_rx_len) &&
         ((s_rx_buffer[consume_len] == (uint8_t)'\n') || (s_rx_buffer[consume_len] == (uint8_t)'\r')))
  {
    consume_len++;
  }

  while ((command_len > 0u) &&
         ((s_rx_buffer[command_len - 1u] == (uint8_t)' ') || (s_rx_buffer[command_len - 1u] == (uint8_t)'\t')))
  {
    command_len--;
  }

  if ((command_len > 0u) && CommandProtocol_ParseText(s_rx_buffer, command_len, request))
  {
    memmove(s_rx_buffer, &s_rx_buffer[consume_len], s_rx_len - consume_len);
    s_rx_len -= consume_len;
    return true;
  }

  memmove(s_rx_buffer, &s_rx_buffer[consume_len], s_rx_len - consume_len);
  s_rx_len -= consume_len;
  return false;
}

bool TcpService_SendResponse(const command_response_t *response)
{
  uint8_t tx[320];
  size_t tx_len = 0u;

  CommandProtocol_FormatResponse(response, tx, sizeof(tx), &tx_len);
  if (tx_len == 0u)
  {
    return false;
  }

  APP_LOG_INFO("TCP response: %s", (const char *)tx);
  return W5500_SocketSend(W5500_SOCKET_CONTROL, tx, tx_len) == tx_len;
}

bool TcpService_CanSendRealtimeSamples(size_t count, sample_precision_t precision)
{
  size_t packet_len;

  if (!TcpService_IsDataConnected() || (count == 0u))
  {
    return false;
  }
  if (count > TCP_REALTIME_MAX_SAMPLES)
  {
    count = TCP_REALTIME_MAX_SAMPLES;
  }

  packet_len = TCP_REALTIME_HEADER_SIZE + (count * TcpService_BytesPerSample(precision));
  return W5500_SocketTxFree(W5500_SOCKET_DATA) >= packet_len;
}

bool TcpService_SendRealtimeSamples(const int32_t *samples, size_t count, sample_precision_t precision)
{
  uint8_t tx[TCP_REALTIME_HEADER_SIZE + (TCP_REALTIME_MAX_SAMPLES * 4u)];
  size_t payload_len = 0u;
  size_t total_len;
  size_t sent;

  if ((samples == 0) || (count == 0u))
  {
    return false;
  }
  if (!TcpService_IsDataConnected())
  {
    return false;
  }
  if (count > TCP_REALTIME_MAX_SAMPLES)
  {
    count = TCP_REALTIME_MAX_SAMPLES;
  }

  tx[0] = 'H';
  tx[1] = 'Y';
  tx[2] = 1u;
  tx[3] = (uint8_t)precision;
  TcpService_WriteLe32(&tx[4], s_stream_sequence++);
  TcpService_WriteLe32(&tx[8], s_realtime_sample_rate_hz);
  TcpService_WriteLe16(&tx[12], (uint16_t)count);
  TcpService_WriteLe16(&tx[14], 0u);

  for (size_t i = 0; i < count; ++i)
  {
    payload_len += TcpService_PackSample(&tx[TCP_REALTIME_HEADER_SIZE + payload_len], samples[i], precision);
  }

  TcpService_WriteLe16(&tx[14], (uint16_t)payload_len);
  total_len = TCP_REALTIME_HEADER_SIZE + payload_len;
  sent = W5500_SocketSend(W5500_SOCKET_DATA, tx, total_len);
  if (sent != total_len)
  {
    s_stream_send_failures++;
    if ((s_stream_send_failures == 1u) || ((s_stream_send_failures & 0x3FFu) == 0u))
    {
      APP_LOG_WARN("TCP realtime send short sent=%u total=%u failures=%u",
                   (unsigned int)sent,
                   (unsigned int)total_len,
                   (unsigned int)s_stream_send_failures);
    }
    return false;
  }

  return true;
}

bool TcpService_IsControlConnected(void)
{
  return W5500_SocketStatus(W5500_SOCKET_CONTROL) == W5500_SOCK_SR_ESTABLISHED;
}

bool TcpService_IsDataConnected(void)
{
  return W5500_SocketStatus(W5500_SOCKET_DATA) == W5500_SOCK_SR_ESTABLISHED;
}

tcp_service_state_t TcpService_GetState(void)
{
  return s_state;
}
