#include "ring_buffer.h"

#if defined(__GNUC__)
static inline __attribute__((always_inline)) size_t RingBuffer_Next(const ring_buffer_i32_t *rb,
                                                                    size_t index)
#else
static inline size_t RingBuffer_Next(const ring_buffer_i32_t *rb, size_t index)
#endif
{
  size_t next = index + 1u;

  if (next >= rb->capacity)
  {
    next = 0u;
  }
  return next;
}

void RingBuffer_Init(ring_buffer_i32_t *rb, int32_t *storage, size_t capacity)
{
  rb->data = storage;
  rb->capacity = capacity;
  rb->head = 0;
  rb->tail = 0;
}

void RingBuffer_Reset(ring_buffer_i32_t *rb)
{
  rb->head = 0;
  rb->tail = 0;
}

bool RingBuffer_Push(ring_buffer_i32_t *rb, int32_t value)
{
  size_t next = RingBuffer_Next(rb, rb->head);

  if (next == rb->tail)
  {
    return false;
  }

  rb->data[rb->head] = value;
  rb->head = next;
  return true;
}

bool RingBuffer_Pop(ring_buffer_i32_t *rb, int32_t *value)
{
  if (rb->head == rb->tail)
  {
    return false;
  }

  *value = rb->data[rb->tail];
  rb->tail = RingBuffer_Next(rb, rb->tail);
  return true;
}

size_t RingBuffer_Count(const ring_buffer_i32_t *rb)
{
  if (rb->head >= rb->tail)
  {
    return rb->head - rb->tail;
  }

  return rb->capacity - rb->tail + rb->head;
}
