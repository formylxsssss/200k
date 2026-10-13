#include "acquisition.h"
#include "ads127l11.h"
#include "app_config.h"
#include "app_log.h"
#include "range_ctrl.h"
#include "ring_buffer.h"
#include "spi.h"
#include "tim.h"

#define ACQ_BUFFER_SAMPLES 163840u
#define ACQ_RATE_LOG_INTERVAL_MS 1000u
#define ACQ_ISR_OVERLOAD_LIMIT 8u
#define ACQ_MAIN_STALL_MIN_TIMEOUT_MS 250u
#define ACQ_MAIN_STALL_MAX_TIMEOUT_MS 1000u
#define ACQ_MAIN_STALL_BUFFER_PERCENT 75u

#if defined(__GNUC__)
#define ACQ_ALWAYS_INLINE static inline __attribute__((always_inline))
#define ACQ_FAST_CODE __attribute__((hot, optimize("O3")))
#else
#define ACQ_ALWAYS_INLINE static inline
#define ACQ_FAST_CODE
#endif

static ads127l11_t s_adc;
static int32_t s_storage[ACQ_BUFFER_SAMPLES];
static ring_buffer_i32_t s_samples;
static volatile bool s_running;
static volatile bool s_isr_overloaded;
static volatile bool s_main_stalled;
static uint8_t s_consecutive_overload;
static volatile uint64_t s_drdy_events;
static volatile uint64_t s_total_samples;
static volatile uint64_t s_dropped_samples;
static volatile uint64_t s_estimated_missed_samples;
static volatile uint64_t s_read_errors;
static volatile uint32_t s_isr_max_cycles;
static acquisition_settings_t s_settings;
static uint32_t s_actual_sample_rate_hz;
static uint32_t s_start_tick_ms;
static volatile uint32_t s_last_main_task_ms;
static uint32_t s_main_stall_timeout_ms;
static uint32_t s_rate_log_tick_ms;
static uint64_t s_rate_log_drdy;
static uint64_t s_rate_log_samples;
static uint64_t s_rate_log_dropped;
static uint64_t s_rate_log_missed;
static uint64_t s_rate_log_read_errors;
static uint32_t s_expected_drdy_cycles;
static uint32_t s_isr_safe_cycles;
static uint32_t s_last_drdy_cycle;
static bool s_have_drdy_cycle;
static uint32_t s_software_gain_q30;

static void Acquisition_EnableCycleCounter(void)
{
#if defined(CoreDebug_DEMCR_TRCENA_Msk) && defined(DWT_CTRL_CYCCNTENA_Msk)
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0u;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
#endif
}

ACQ_ALWAYS_INLINE uint32_t Acquisition_GetCycleCounter(void)
{
#if defined(DWT_CTRL_CYCCNTENA_Msk)
  return DWT->CYCCNT;
#else
  return 0u;
#endif
}

ACQ_ALWAYS_INLINE void Acquisition_UpdateDrdyTiming(uint32_t now_cycle)
{
  uint32_t elapsed_cycles;
  uint32_t elapsed_periods;

  if (s_expected_drdy_cycles == 0u)
  {
    return;
  }

  if (s_have_drdy_cycle)
  {
    elapsed_cycles = now_cycle - s_last_drdy_cycle;
    if (elapsed_cycles > (s_expected_drdy_cycles + (s_expected_drdy_cycles / 2u)))
    {
      elapsed_periods = (elapsed_cycles + (s_expected_drdy_cycles / 2u)) / s_expected_drdy_cycles;
      if ((elapsed_periods > 1u) && (elapsed_periods < 1000000u))
      {
        s_estimated_missed_samples += (uint64_t)(elapsed_periods - 1u);
      }
    }
  }

  s_last_drdy_cycle = now_cycle;
  s_have_drdy_cycle = true;
}

ACQ_ALWAYS_INLINE bool Acquisition_BufferSample(int32_t sample)
{
  size_t head = s_samples.head;
  size_t next = head + 1u;

  if (next >= ACQ_BUFFER_SAMPLES)
  {
    next = 0u;
  }

  if (next == s_samples.tail)
  {
    return false;
  }

  s_storage[head] = sample;
  __DMB();
  s_samples.head = next;
  return true;
}

ACQ_ALWAYS_INLINE int32_t Acquisition_ApplySoftwareGain(int32_t sample)
{
  uint64_t magnitude;
  uint64_t scaled;

  if (s_software_gain_q30 == 1073741824u)
  {
    return sample;
  }

  magnitude = (sample < 0) ? (uint64_t)(-(int64_t)sample) : (uint64_t)sample;
  scaled = ((magnitude * s_software_gain_q30) + (1ull << 29u)) >> 30u;
  return (sample < 0) ? -(int32_t)scaled : (int32_t)scaled;
}

static void Acquisition_LogRate(void)
{
  uint32_t now_ms = HAL_GetTick();
  uint32_t elapsed_ms = now_ms - s_rate_log_tick_ms;
  acquisition_stats_t stats;
  uint64_t delta_drdy;
  uint64_t delta_samples;
  uint64_t delta_dropped;
  uint64_t delta_missed;
  uint64_t delta_errors;
  uint32_t measured_sps;

  if (elapsed_ms < ACQ_RATE_LOG_INTERVAL_MS)
  {
    return;
  }

  Acquisition_GetStats(&stats);
  delta_drdy = stats.drdy_events - s_rate_log_drdy;
  delta_samples = stats.samples_captured - s_rate_log_samples;
  delta_dropped = stats.ring_dropped - s_rate_log_dropped;
  delta_missed = stats.estimated_missed - s_rate_log_missed;
  delta_errors = stats.read_errors - s_rate_log_read_errors;
  measured_sps = (uint32_t)(((uint64_t)delta_samples * 1000u) / elapsed_ms);

  APP_LOG_INFO("acq rate target=%u actual_clk=%u measured=%u sps drdy/s=%u buffered=%u/%u dropped/s=%u missed/s=%u errors/s=%u isr_max=%u/%u cyc total_low=%u",
               (unsigned int)s_settings.sample_rate_hz,
               (unsigned int)s_actual_sample_rate_hz,
               (unsigned int)measured_sps,
               (unsigned int)((delta_drdy * 1000u) / elapsed_ms),
               (unsigned int)stats.buffered_samples,
               (unsigned int)stats.buffer_capacity_samples,
               (unsigned int)delta_dropped,
               (unsigned int)delta_missed,
               (unsigned int)delta_errors,
               (unsigned int)stats.isr_max_cycles,
               (unsigned int)stats.isr_cycle_budget,
               (unsigned int)(stats.samples_captured & 0xFFFFFFFFu));

  s_rate_log_tick_ms = now_ms;
  s_rate_log_drdy = stats.drdy_events;
  s_rate_log_samples = stats.samples_captured;
  s_rate_log_dropped = stats.ring_dropped;
  s_rate_log_missed = stats.estimated_missed;
  s_rate_log_read_errors = stats.read_errors;
}

static uint32_t Acquisition_CalculateMainStallTimeout(uint32_t sample_rate_hz)
{
  uint32_t timeout_ms;
  uint64_t buffer_hold_ms;

  if (sample_rate_hz == 0u)
  {
    return ACQ_MAIN_STALL_MIN_TIMEOUT_MS;
  }

  buffer_hold_ms = ((uint64_t)(ACQ_BUFFER_SAMPLES - 1u) * 1000u) / sample_rate_hz;
  timeout_ms = (uint32_t)((buffer_hold_ms * ACQ_MAIN_STALL_BUFFER_PERCENT) / 100u);
  if (timeout_ms < ACQ_MAIN_STALL_MIN_TIMEOUT_MS)
  {
    timeout_ms = ACQ_MAIN_STALL_MIN_TIMEOUT_MS;
  }
  if (timeout_ms > ACQ_MAIN_STALL_MAX_TIMEOUT_MS)
  {
    timeout_ms = ACQ_MAIN_STALL_MAX_TIMEOUT_MS;
  }
  return timeout_ms;
}

static bool Acquisition_SetAdcClock(uint32_t sample_rate_hz)
{
  uint32_t timer_clk_hz = HAL_RCC_GetPCLK1Freq();
  uint64_t target_clk_hz = (uint64_t)sample_rate_hz * APP_ADC_CLK_PER_SAMPLE;
  uint32_t period_counts;
  uint32_t actual_adc_clk_hz;

  if ((sample_rate_hz == 0u) || (target_clk_hz == 0u))
  {
    return false;
  }

  period_counts = (uint32_t)(((uint64_t)timer_clk_hz + (target_clk_hz / 2u)) / target_clk_hz);
  if (period_counts < 2u)
  {
    period_counts = 2u;
  }
  if (period_counts > 65536u)
  {
    period_counts = 65536u;
  }

  (void)HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_3);
  __HAL_TIM_SET_PRESCALER(&htim3, 0u);
  __HAL_TIM_SET_AUTORELOAD(&htim3, period_counts - 1u);
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, period_counts / 2u);
  __HAL_TIM_SET_COUNTER(&htim3, 0u);

  if (HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3) != HAL_OK)
  {
    APP_LOG_ERR("ADC clock PWM restart failed");
    return false;
  }

  actual_adc_clk_hz = timer_clk_hz / period_counts;
  s_actual_sample_rate_hz = actual_adc_clk_hz / APP_ADC_CLK_PER_SAMPLE;
  s_expected_drdy_cycles = (s_actual_sample_rate_hz > 0u) ?
                           (SystemCoreClock / s_actual_sample_rate_hz) : 0u;
  s_isr_safe_cycles = (s_expected_drdy_cycles * 4u) / 5u;
  APP_LOG_OK("ADC clock set target_rate=%u actual_rate=%u adc_clk=%u period=%u",
             (unsigned int)sample_rate_hz,
             (unsigned int)s_actual_sample_rate_hz,
             (unsigned int)actual_adc_clk_hz,
             (unsigned int)period_counts);
  return true;
}

void Acquisition_Init(void)
{
  APP_LOG_INFO("Acquisition_Init buffer=%u samples bytes=%u max_hold_ms_at_200k=%u",
               (unsigned int)ACQ_BUFFER_SAMPLES,
               (unsigned int)sizeof(s_storage),
               (unsigned int)((ACQ_BUFFER_SAMPLES * 1000u) / SYSTEM_SAMPLE_RATE_MAX_HZ));

  s_settings = *SystemConfig_GetDefaultAcquisition();
  RingBuffer_Init(&s_samples, s_storage, ACQ_BUFFER_SAMPLES);
  Acquisition_EnableCycleCounter();
  if (ADS127L11_Init(&s_adc, &hspi1) != HAL_OK)
  {
    APP_LOG_ERR("ADS127L11 init failed");
    Error_Handler();
  }

  s_drdy_events = 0u;
  s_total_samples = 0u;
  s_dropped_samples = 0u;
  s_estimated_missed_samples = 0u;
  s_read_errors = 0u;
  s_isr_max_cycles = 0u;
  s_actual_sample_rate_hz = s_settings.sample_rate_hz;
  s_software_gain_q30 = SystemConfig_GetSoftwareGainQ30(&s_settings);
  s_expected_drdy_cycles = (s_actual_sample_rate_hz > 0u) ?
                           (SystemCoreClock / s_actual_sample_rate_hz) : 0u;
  s_isr_safe_cycles = (s_expected_drdy_cycles * 4u) / 5u;
  s_isr_overloaded = false;
  s_main_stalled = false;
  s_consecutive_overload = 0u;
  s_start_tick_ms = 0u;
  s_rate_log_tick_ms = 0u;
  s_rate_log_drdy = 0u;
  s_rate_log_samples = 0u;
  s_rate_log_dropped = 0u;
  s_rate_log_missed = 0u;
  s_rate_log_read_errors = 0u;
  s_last_drdy_cycle = 0u;
  s_have_drdy_cycle = false;
  APP_LOG_OK("Acquisition_Init done");
}

bool Acquisition_Configure(const acquisition_settings_t *settings)
{
  int32_t nominal_gain_db = 0;
  int32_t hardware_gain_db = 0;
  int32_t software_gain_db = 0;

  if (!SystemConfig_ValidateAcquisition(settings))
  {
    APP_LOG_ERR("Acquisition_Configure invalid");
    return false;
  }

  s_settings = *settings;
  RangeCtrl_SetSwitchBits(settings->range_switch_bits);
  s_software_gain_q30 = SystemConfig_GetSoftwareGainQ30(settings);

  if (!Acquisition_SetAdcClock(settings->sample_rate_hz))
  {
    return false;
  }

  if (ADS127L11_ConfigureAcquisition(&s_adc, settings) != HAL_OK)
  {
    return false;
  }

  if (SystemConfig_GetNominalGainDb(settings, &nominal_gain_db))
  {
    (void)SystemConfig_GetHardwareGainDb(settings, &hardware_gain_db);
    software_gain_db = SystemConfig_GetSoftwareGainDb(settings);
    APP_LOG_OK("Acquisition config rate=%u precision=%s gain=%+dDB hw=%+dDB sw=%+dDB branch=0x%02X",
               (unsigned int)settings->sample_rate_hz,
               SamplePrecision_ToString(settings->precision),
               (int)nominal_gain_db,
               (int)hardware_gain_db,
               (int)software_gain_db,
               (unsigned int)(settings->range_switch_bits & 0x0Fu));
  }
  else
  {
    APP_LOG_OK("Acquisition config rate=%u precision=%s gain=INVALID branch=0x%02X",
               (unsigned int)settings->sample_rate_hz,
               SamplePrecision_ToString(settings->precision),
               (unsigned int)(settings->range_switch_bits & 0x0Fu));
  }
  return true;
}

void Acquisition_Start(void)
{
  HAL_NVIC_DisableIRQ(EXTI1_IRQn);
  s_running = false;
  s_isr_overloaded = false;
  s_main_stalled = false;
  s_consecutive_overload = 0u;
  RingBuffer_Reset(&s_samples);
  s_drdy_events = 0u;
  s_total_samples = 0u;
  s_dropped_samples = 0u;
  s_estimated_missed_samples = 0u;
  s_read_errors = 0u;
  s_isr_max_cycles = 0u;
  s_start_tick_ms = HAL_GetTick();
  s_last_main_task_ms = s_start_tick_ms;
  s_main_stall_timeout_ms = Acquisition_CalculateMainStallTimeout(s_settings.sample_rate_hz);
  s_rate_log_tick_ms = s_start_tick_ms;
  s_rate_log_drdy = 0u;
  s_rate_log_samples = 0u;
  s_rate_log_dropped = 0u;
  s_rate_log_missed = 0u;
  s_rate_log_read_errors = 0u;
  s_last_drdy_cycle = 0u;
  s_have_drdy_cycle = false;
  __HAL_GPIO_EXTI_CLEAR_IT(ADS_DRDY_Pin);

  ADS127L11_SyncPulse();
  HAL_Delay(1);
  __HAL_GPIO_EXTI_CLEAR_IT(ADS_DRDY_Pin);
  NVIC_ClearPendingIRQ(EXTI1_IRQn);

  APP_LOG_INFO("Acquisition_Start sync done target=%uHz drdy=%u stall_timeout=%ums",
               (unsigned int)s_settings.sample_rate_hz,
               ADS127L11_DataReadyLevel() ? 1u : 0u,
               (unsigned int)s_main_stall_timeout_ms);
  s_running = true;
  HAL_NVIC_EnableIRQ(EXTI1_IRQn);

  APP_LOG_OK("Acquisition_Start target=%uHz actual_clk=%uHz drdy=%u",
             (unsigned int)s_settings.sample_rate_hz,
             (unsigned int)s_actual_sample_rate_hz,
             ADS127L11_DataReadyLevel() ? 1u : 0u);
}

void Acquisition_Stop(void)
{
  uint32_t elapsed_ms = HAL_GetTick() - s_start_tick_ms;
  uint32_t avg_sps = 0u;
  acquisition_stats_t stats;

  HAL_NVIC_DisableIRQ(EXTI1_IRQn);
  s_running = false;
  __HAL_GPIO_EXTI_CLEAR_IT(ADS_DRDY_Pin);
  NVIC_ClearPendingIRQ(EXTI1_IRQn);
  Acquisition_GetStats(&stats);

  if (elapsed_ms > 0u)
  {
    avg_sps = (uint32_t)((stats.samples_captured * 1000u) / elapsed_ms);
  }

  APP_LOG_WARN("Acquisition_Stop total_low=%u avg=%u sps elapsed=%u ms buffered=%u dropped_low=%u missed_low=%u errors_low=%u isr_max=%u/%u cyc",
               (unsigned int)(stats.samples_captured & 0xFFFFFFFFu),
               (unsigned int)avg_sps,
               (unsigned int)elapsed_ms,
               (unsigned int)stats.buffered_samples,
               (unsigned int)(stats.ring_dropped & 0xFFFFFFFFu),
               (unsigned int)(stats.estimated_missed & 0xFFFFFFFFu),
               (unsigned int)(stats.read_errors & 0xFFFFFFFFu),
               (unsigned int)stats.isr_max_cycles,
               (unsigned int)stats.isr_cycle_budget);
  s_isr_overloaded = false;
  s_main_stalled = false;
}

ACQ_FAST_CODE void Acquisition_DrdyIsr(void)
{
  int32_t sample = 0;
  uint32_t isr_start_cycle;
  uint32_t isr_cycles;

  if (!s_running)
  {
    return;
  }

  isr_start_cycle = Acquisition_GetCycleCounter();
  s_drdy_events++;
  Acquisition_UpdateDrdyTiming(isr_start_cycle);
  if (ADS127L11_ReadSampleFast(&s_adc, &sample) == HAL_OK)
  {
    sample = Acquisition_ApplySoftwareGain(sample);
    s_total_samples++;
    if (!Acquisition_BufferSample(sample))
    {
      s_dropped_samples++;
    }
  }
  else
  {
    s_read_errors++;
  }

  isr_cycles = Acquisition_GetCycleCounter() - isr_start_cycle;
  if (isr_cycles > s_isr_max_cycles)
  {
    s_isr_max_cycles = isr_cycles;
  }
  if ((s_isr_safe_cycles != 0u) && (isr_cycles >= s_isr_safe_cycles))
  {
    if (++s_consecutive_overload >= ACQ_ISR_OVERLOAD_LIMIT)
    {
      s_isr_overloaded = true;
      s_running = false;
      NVIC_DisableIRQ(EXTI1_IRQn);
    }
  }
  else
  {
    s_consecutive_overload = 0u;
  }
}

void Acquisition_Task(void)
{
  if (!s_running)
  {
    return;
  }

  s_last_main_task_ms = HAL_GetTick();
  Acquisition_LogRate();
}

void Acquisition_WatchdogTick(uint32_t now_ms)
{
  if (s_running && ((now_ms - s_last_main_task_ms) > s_main_stall_timeout_ms))
  {
    s_main_stalled = true;
    s_isr_overloaded = true;
    s_running = false;
    NVIC_DisableIRQ(EXTI1_IRQn);
  }
}

bool Acquisition_ReadSample(int32_t *sample)
{
  return RingBuffer_Pop(&s_samples, sample);
}

void Acquisition_DiscardBufferedSamples(void)
{
  uint32_t primask = __get_PRIMASK();

  __disable_irq();
  RingBuffer_Reset(&s_samples);
  if (primask == 0u)
  {
    __enable_irq();
  }
}

bool Acquisition_IsRunning(void)
{
  return s_running;
}

bool Acquisition_IsOverloaded(void)
{
  return s_isr_overloaded;
}

bool Acquisition_IsMainStalled(void)
{
  return s_main_stalled;
}

size_t Acquisition_GetBufferedSamples(void)
{
  return RingBuffer_Count(&s_samples);
}

size_t Acquisition_GetBufferCapacitySamples(void)
{
  return ACQ_BUFFER_SAMPLES - 1u;
}

void Acquisition_GetStats(acquisition_stats_t *stats)
{
  uint32_t primask;

  if (stats == 0)
  {
    return;
  }

  primask = __get_PRIMASK();
  __disable_irq();
  stats->drdy_events = s_drdy_events;
  stats->samples_captured = s_total_samples;
  stats->ring_dropped = s_dropped_samples;
  stats->estimated_missed = s_estimated_missed_samples;
  stats->read_errors = s_read_errors;
  stats->isr_max_cycles = s_isr_max_cycles;
  stats->isr_cycle_budget = s_expected_drdy_cycles;
  stats->buffered_samples = RingBuffer_Count(&s_samples);
  if (primask == 0u)
  {
    __enable_irq();
  }
  stats->buffer_capacity_samples = ACQ_BUFFER_SAMPLES - 1u;
}

const acquisition_settings_t *Acquisition_GetSettings(void)
{
  return &s_settings;
}

uint32_t Acquisition_GetActualSampleRate(void)
{
  return s_actual_sample_rate_hz;
}
