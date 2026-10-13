#include "format_utils.h"

bool Format_U64Decimal(uint64_t value, char *buffer, size_t buffer_len)
{
  char reverse[20];
  size_t digits = 0u;

  if ((buffer == 0) || (buffer_len == 0u))
  {
    return false;
  }

  do
  {
    reverse[digits++] = (char)('0' + (value % 10u));
    value /= 10u;
  } while ((value != 0u) && (digits < sizeof(reverse)));

  if (buffer_len <= digits)
  {
    buffer[0] = '\0';
    return false;
  }

  for (size_t i = 0u; i < digits; ++i)
  {
    buffer[i] = reverse[digits - 1u - i];
  }
  buffer[digits] = '\0';
  return true;
}
