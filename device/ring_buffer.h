#ifndef __RING_BUFFER_H__
#define __RING_BUFFER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
  int32_t *data;
  size_t capacity;
  volatile size_t head;
  volatile size_t tail;
} ring_buffer_i32_t;

void RingBuffer_Init(ring_buffer_i32_t *rb, int32_t *storage, size_t capacity);
void RingBuffer_Reset(ring_buffer_i32_t *rb);
bool RingBuffer_Push(ring_buffer_i32_t *rb, int32_t value);
bool RingBuffer_Pop(ring_buffer_i32_t *rb, int32_t *value);
size_t RingBuffer_Count(const ring_buffer_i32_t *rb);

#ifdef __cplusplus
}
#endif

#endif /* __RING_BUFFER_H__ */
