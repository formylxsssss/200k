#ifndef __ACQUISITION_H__
#define __ACQUISITION_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "system_config.h"

typedef struct
{
  uint64_t drdy_events;
  uint64_t samples_captured;
  uint64_t ring_dropped;
  uint64_t estimated_missed;
  uint64_t read_errors;
  uint32_t isr_max_cycles;
  uint32_t isr_cycle_budget;
  size_t buffered_samples;
  size_t buffer_capacity_samples;
} acquisition_stats_t;

void Acquisition_Init(void);
bool Acquisition_Configure(const acquisition_settings_t *settings);
void Acquisition_Start(void);
void Acquisition_Stop(void);
void Acquisition_DrdyIsr(void);
void Acquisition_Task(void);
bool Acquisition_ReadSample(int32_t *sample);
void Acquisition_DiscardBufferedSamples(void);
bool Acquisition_IsRunning(void);
bool Acquisition_IsOverloaded(void);
bool Acquisition_IsMainStalled(void);
void Acquisition_WatchdogTick(uint32_t now_ms);
size_t Acquisition_GetBufferedSamples(void);
size_t Acquisition_GetBufferCapacitySamples(void);
void Acquisition_GetStats(acquisition_stats_t *stats);
const acquisition_settings_t *Acquisition_GetSettings(void);
uint32_t Acquisition_GetActualSampleRate(void);

#ifdef __cplusplus
}
#endif

#endif /* __ACQUISITION_H__ */
