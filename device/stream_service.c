#include "stream_service.h"
#include "acquisition.h"
#include "app_log.h"
#include "tcp_service.h"

#define STREAM_BATCH_SAMPLES       256u
#define STREAM_MAX_PACKETS_PER_TASK 4u
#define STREAM_RATE_LOG_INTERVAL_MS 1000u
#define STREAM_PACKET_HEADER_BYTES  16u

static bool s_stream_active;
static acquisition_settings_t s_stream_settings;
static uint64_t s_stream_sent_samples;
static uint64_t s_stream_sent_bytes;
static uint32_t s_stream_sent_packets;
static uint32_t s_stream_start_tick_ms;
static uint32_t s_stream_rate_log_tick_ms;
static uint64_t s_stream_rate_log_samples;
static uint64_t s_stream_rate_log_bytes;
static uint32_t s_stream_rate_log_packets;

static size_t StreamService_BytesPerSample(sample_precision_t precision)
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

static void StreamService_LogRate(void)
{
  uint32_t now_ms = HAL_GetTick();
  uint32_t elapsed_ms = now_ms - s_stream_rate_log_tick_ms;
  uint64_t delta_samples;
  uint64_t delta_bytes;
  uint32_t delta_packets;
  uint32_t tx_sps;
  uint32_t tx_kbps;

  if (elapsed_ms < STREAM_RATE_LOG_INTERVAL_MS)
  {
    return;
  }

  delta_samples = s_stream_sent_samples - s_stream_rate_log_samples;
  delta_bytes = s_stream_sent_bytes - s_stream_rate_log_bytes;
  delta_packets = s_stream_sent_packets - s_stream_rate_log_packets;
  tx_sps = (uint32_t)((delta_samples * 1000u) / elapsed_ms);
  tx_kbps = (uint32_t)((delta_bytes * 1000u) / ((uint64_t)elapsed_ms * 1024u));

  APP_LOG_INFO("tx rate target=%u actual_clk=%u sent=%u sps data=%u kB/s packets/s=%u total_low=%u bytes_low=%u",
               (unsigned int)s_stream_settings.sample_rate_hz,
               (unsigned int)Acquisition_GetActualSampleRate(),
               (unsigned int)tx_sps,
               (unsigned int)tx_kbps,
               (unsigned int)delta_packets,
               (unsigned int)(s_stream_sent_samples & 0xFFFFFFFFu),
               (unsigned int)(s_stream_sent_bytes & 0xFFFFFFFFu));

  s_stream_rate_log_tick_ms = now_ms;
  s_stream_rate_log_samples = s_stream_sent_samples;
  s_stream_rate_log_bytes = s_stream_sent_bytes;
  s_stream_rate_log_packets = s_stream_sent_packets;
}

void StreamService_Init(void)
{
  s_stream_active = false;
  s_stream_sent_samples = 0u;
  s_stream_sent_bytes = 0u;
  s_stream_sent_packets = 0u;
  s_stream_start_tick_ms = 0u;
  s_stream_rate_log_tick_ms = 0u;
  s_stream_rate_log_samples = 0u;
  s_stream_rate_log_bytes = 0u;
  s_stream_rate_log_packets = 0u;
  APP_LOG_INFO("StreamService init");
}

bool StreamService_Start(const acquisition_settings_t *settings)
{
  acquisition_settings_t tx_settings;
  int32_t nominal_gain_db = 0;

  if ((settings == 0) || !SystemConfig_ValidateAcquisition(settings))
  {
    APP_LOG_ERR("stream start invalid settings");
    return false;
  }

  s_stream_settings = *settings;
  tx_settings = *settings;
  tx_settings.sample_rate_hz = Acquisition_GetActualSampleRate();
  s_stream_sent_samples = 0u;
  s_stream_sent_bytes = 0u;
  s_stream_sent_packets = 0u;
  s_stream_start_tick_ms = HAL_GetTick();
  s_stream_rate_log_tick_ms = s_stream_start_tick_ms;
  s_stream_rate_log_samples = 0u;
  s_stream_rate_log_bytes = 0u;
  s_stream_rate_log_packets = 0u;
  s_stream_active = true;
  TcpService_SetRealtimeConfig(&tx_settings);
  if (SystemConfig_GetNominalGainDb(settings, &nominal_gain_db))
  {
    APP_LOG_OK("stream start target_rate=%u actual_rate=%u precision=%s gain=%+dDB",
               (unsigned int)settings->sample_rate_hz,
               (unsigned int)tx_settings.sample_rate_hz,
               SamplePrecision_ToString(settings->precision),
               (int)nominal_gain_db);
  }
  else
  {
    APP_LOG_OK("stream start target_rate=%u actual_rate=%u precision=%s gain=CUSTOM",
               (unsigned int)settings->sample_rate_hz,
               (unsigned int)tx_settings.sample_rate_hz,
               SamplePrecision_ToString(settings->precision));
  }
  return true;
}

void StreamService_Stop(void)
{
  if (s_stream_active)
  {
    uint32_t elapsed_ms = HAL_GetTick() - s_stream_start_tick_ms;
    uint32_t avg_sps = 0u;
    uint32_t avg_kbps = 0u;

    if (elapsed_ms > 0u)
    {
      avg_sps = (uint32_t)((s_stream_sent_samples * 1000u) / elapsed_ms);
      avg_kbps = (uint32_t)((s_stream_sent_bytes * 1000u) / ((uint64_t)elapsed_ms * 1024u));
    }

    APP_LOG_WARN("stream stop samples_low=%u avg=%u sps avg_data=%u kB/s packets=%u bytes_low=%u elapsed=%u ms",
                 (unsigned int)(s_stream_sent_samples & 0xFFFFFFFFu),
                 (unsigned int)avg_sps,
                 (unsigned int)avg_kbps,
                 (unsigned int)s_stream_sent_packets,
                 (unsigned int)(s_stream_sent_bytes & 0xFFFFFFFFu),
                 (unsigned int)elapsed_ms);
  }
  s_stream_active = false;
}

void StreamService_Task(void)
{
  int32_t batch[STREAM_BATCH_SAMPLES];
  int32_t sample;
  uint32_t packets_sent = 0u;

  if (!s_stream_active)
  {
    return;
  }
  if (!TcpService_IsDataConnected())
  {
    return;
  }

  while ((packets_sent < STREAM_MAX_PACKETS_PER_TASK) &&
         TcpService_CanSendRealtimeSamples(STREAM_BATCH_SAMPLES, s_stream_settings.precision))
  {
    size_t count = 0u;

    if (Acquisition_GetBufferedSamples() < STREAM_BATCH_SAMPLES)
    {
      break;
    }

    while ((count < STREAM_BATCH_SAMPLES) && Acquisition_ReadSample(&sample))
    {
      batch[count++] = sample;
    }

    if (count == 0u)
    {
      break;
    }

    if (!TcpService_SendRealtimeSamples(batch, count, s_stream_settings.precision))
    {
      break;
    }

    s_stream_sent_samples += count;
    s_stream_sent_bytes += STREAM_PACKET_HEADER_BYTES + (count * StreamService_BytesPerSample(s_stream_settings.precision));
    s_stream_sent_packets++;
    packets_sent++;
  }

  StreamService_LogRate();
}

bool StreamService_IsActive(void)
{
  return s_stream_active;
}
