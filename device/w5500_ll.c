#include "w5500_ll.h"
#include "app_config.h"
#include "app_log.h"
#include "main.h"
#include <stdbool.h>

#define W5500_CTL_READ(block)   (uint8_t)(((block) << 3) | 0x00u)
#define W5500_CTL_WRITE(block)  (uint8_t)(((block) << 3) | 0x04u)

#define W5500_REG_PHYCFGR       0x002Eu

#define W5500_SOCK_REG_MR       0x0000u
#define W5500_SOCK_REG_CR       0x0001u
#define W5500_SOCK_REG_IR       0x0002u
#define W5500_SOCK_REG_SR       0x0003u
#define W5500_SOCK_REG_PORT     0x0004u
#define W5500_SOCK_REG_RXBUF    0x001Eu
#define W5500_SOCK_REG_TXBUF    0x001Fu
#define W5500_SOCK_REG_TX_FSR   0x0020u
#define W5500_SOCK_REG_TX_WR    0x0024u
#define W5500_SOCK_REG_RX_RSR   0x0026u
#define W5500_SOCK_REG_RX_RD    0x0028u

#define W5500_SOCK_MR_TCP       0x01u
#define W5500_SOCK_CR_OPEN      0x01u
#define W5500_SOCK_CR_LISTEN    0x02u
#define W5500_SOCK_CR_DISCON    0x08u
#define W5500_SOCK_CR_CLOSE     0x10u
#define W5500_SOCK_CR_SEND      0x20u
#define W5500_SOCK_CR_RECV      0x40u
#define W5500_SOCK_IR_ALL       0x1Fu

#define W5500_MAX_SOCKET_ID     7u
#define W5500_MEM_UNIT_BYTES    1024u
#define W5500_CMD_WAIT_LIMIT    20000u

static uint8_t s_tx_kb[W5500_MAX_SOCKET_ID + 1u] = {2u, 8u, 2u, 2u, 2u, 0u, 0u, 0u};
static uint8_t s_rx_kb[W5500_MAX_SOCKET_ID + 1u] = {2u, 1u, 2u, 2u, 2u, 0u, 0u, 0u};

#if defined(__GNUC__)
#define W5500_ALWAYS_INLINE static inline __attribute__((always_inline))
#define W5500_FAST_CODE __attribute__((hot, optimize("O3")))
#else
#define W5500_ALWAYS_INLINE static inline
#define W5500_FAST_CODE
#endif

W5500_ALWAYS_INLINE void W5500_SpiDelay(void)
{
  for (uint32_t i = 0u; i < APP_W5500_SPI_DELAY_CYCLES; ++i)
  {
    __NOP();
  }
}

W5500_ALWAYS_INLINE void W5500_SoftSpiWriteBit(uint8_t value, uint8_t mask)
{
  W5500_MOSI_GPIO_Port->BSRR = ((value & mask) != 0u) ?
    (uint32_t)W5500_MOSI_Pin : ((uint32_t)W5500_MOSI_Pin << 16u);
  W5500_SpiDelay();
  W5500_SCLK_GPIO_Port->BSRR = W5500_SCLK_Pin;
  W5500_SpiDelay();
  W5500_SCLK_GPIO_Port->BSRR = ((uint32_t)W5500_SCLK_Pin << 16u);
  W5500_SpiDelay();
}

W5500_ALWAYS_INLINE uint8_t W5500_SoftSpiReadBit(void)
{
  uint8_t bit;

  W5500_SCLK_GPIO_Port->BSRR = W5500_SCLK_Pin;
  W5500_SpiDelay();
  bit = ((W5500_MISO_GPIO_Port->IDR & W5500_MISO_Pin) != 0u) ? 1u : 0u;
  W5500_SCLK_GPIO_Port->BSRR = ((uint32_t)W5500_SCLK_Pin << 16u);
  W5500_SpiDelay();
  return bit;
}

W5500_ALWAYS_INLINE void W5500_SoftSpiWriteByte(uint8_t value)
{
  W5500_SoftSpiWriteBit(value, 0x80u);
  W5500_SoftSpiWriteBit(value, 0x40u);
  W5500_SoftSpiWriteBit(value, 0x20u);
  W5500_SoftSpiWriteBit(value, 0x10u);
  W5500_SoftSpiWriteBit(value, 0x08u);
  W5500_SoftSpiWriteBit(value, 0x04u);
  W5500_SoftSpiWriteBit(value, 0x02u);
  W5500_SoftSpiWriteBit(value, 0x01u);
}

W5500_ALWAYS_INLINE uint8_t W5500_SoftSpiReadByte(void)
{
  uint8_t value = 0u;

  W5500_MOSI_GPIO_Port->BSRR = W5500_MOSI_Pin;
  value = (uint8_t)((value << 1u) | W5500_SoftSpiReadBit());
  value = (uint8_t)((value << 1u) | W5500_SoftSpiReadBit());
  value = (uint8_t)((value << 1u) | W5500_SoftSpiReadBit());
  value = (uint8_t)((value << 1u) | W5500_SoftSpiReadBit());
  value = (uint8_t)((value << 1u) | W5500_SoftSpiReadBit());
  value = (uint8_t)((value << 1u) | W5500_SoftSpiReadBit());
  value = (uint8_t)((value << 1u) | W5500_SoftSpiReadBit());
  value = (uint8_t)((value << 1u) | W5500_SoftSpiReadBit());
  return value;
}

W5500_ALWAYS_INLINE void W5500_FrameBegin(uint16_t address, uint8_t control)
{
  W5500_CS_GPIO_Port->BSRR = ((uint32_t)W5500_CS_Pin << 16u);
  W5500_SoftSpiWriteByte((uint8_t)(address >> 8));
  W5500_SoftSpiWriteByte((uint8_t)address);
  W5500_SoftSpiWriteByte(control);
}

W5500_ALWAYS_INLINE void W5500_FrameEnd(void)
{
  W5500_CS_GPIO_Port->BSRR = W5500_CS_Pin;
}

static W5500_FAST_CODE void W5500_ReadFrame(uint16_t address, uint8_t control,
                                            uint8_t *buffer, size_t len)
{
  W5500_FrameBegin(address, control);
  for (size_t i = 0; i < len; ++i)
  {
    buffer[i] = W5500_SoftSpiReadByte();
  }
  W5500_FrameEnd();
}

static W5500_FAST_CODE void W5500_WriteFrame(uint16_t address, uint8_t control,
                                             const uint8_t *buffer, size_t len)
{
  W5500_FrameBegin(address, control);
  for (size_t i = 0; i < len; ++i)
  {
    W5500_SoftSpiWriteByte(buffer[i]);
  }
  W5500_FrameEnd();
}

void W5500_Reset(void)
{
  APP_LOG_INFO("W5500 reset assert");
  HAL_GPIO_WritePin(W5500_RESET_GPIO_Port, W5500_RESET_Pin, GPIO_PIN_RESET);
  HAL_Delay(5);
  HAL_GPIO_WritePin(W5500_RESET_GPIO_Port, W5500_RESET_Pin, GPIO_PIN_SET);
  HAL_Delay(50);
  APP_LOG_OK("W5500 reset done");
}

uint8_t W5500_Read8(uint16_t address, uint8_t block)
{
  uint8_t value = 0;

  W5500_Read(address, block, &value, 1);
  return value;
}

void W5500_Write8(uint16_t address, uint8_t block, uint8_t value)
{
  W5500_Write(address, block, &value, 1);
}

void W5500_Read(uint16_t address, uint8_t block, uint8_t *buffer, size_t len)
{
  W5500_ReadFrame(address, W5500_CTL_READ(block), buffer, len);
}

void W5500_Write(uint16_t address, uint8_t block, const uint8_t *buffer, size_t len)
{
  W5500_WriteFrame(address, W5500_CTL_WRITE(block), buffer, len);
}

static bool W5500_IsSocketValid(uint8_t socket_id)
{
  return socket_id <= W5500_MAX_SOCKET_ID;
}

static uint8_t W5500_SocketRegBlock(uint8_t socket_id)
{
  return (uint8_t)(0x01u + (socket_id * 4u));
}

static uint8_t W5500_SocketTxBlock(uint8_t socket_id)
{
  return (uint8_t)(0x02u + (socket_id * 4u));
}

static uint8_t W5500_SocketRxBlock(uint8_t socket_id)
{
  return (uint8_t)(0x03u + (socket_id * 4u));
}

static uint16_t W5500_Read16(uint16_t address, uint8_t block)
{
  uint8_t bytes[2];

  W5500_Read(address, block, bytes, sizeof(bytes));
  return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static void W5500_Write16(uint16_t address, uint8_t block, uint16_t value)
{
  uint8_t bytes[2];

  bytes[0] = (uint8_t)(value >> 8);
  bytes[1] = (uint8_t)(value & 0xFFu);
  W5500_Write(address, block, bytes, sizeof(bytes));
}

static HAL_StatusTypeDef W5500_SocketCommand(uint8_t socket_id, uint8_t command)
{
  uint8_t block;
  uint32_t wait = W5500_CMD_WAIT_LIMIT;

  if (!W5500_IsSocketValid(socket_id))
  {
    return HAL_ERROR;
  }

  block = W5500_SocketRegBlock(socket_id);
  W5500_Write8(W5500_SOCK_REG_CR, block, command);
  while ((W5500_Read8(W5500_SOCK_REG_CR, block) != 0u) && (wait > 0u))
  {
    wait--;
  }

  return (wait > 0u) ? HAL_OK : HAL_TIMEOUT;
}

static uint16_t W5500_SocketMemMask(uint8_t size_kb)
{
  uint16_t bytes = (uint16_t)size_kb * W5500_MEM_UNIT_BYTES;

  if (bytes == 0u)
  {
    bytes = 2u * W5500_MEM_UNIT_BYTES;
  }

  return (uint16_t)(bytes - 1u);
}

static void W5500_ClearSocketMemoryMap(void)
{
  for (uint8_t socket_id = 0u; socket_id <= W5500_MAX_SOCKET_ID; ++socket_id)
  {
    uint8_t block = W5500_SocketRegBlock(socket_id);

    W5500_Write8(W5500_SOCK_REG_RXBUF, block, 0u);
    W5500_Write8(W5500_SOCK_REG_TXBUF, block, 0u);
    s_rx_kb[socket_id] = 0u;
    s_tx_kb[socket_id] = 0u;
  }
}

HAL_StatusTypeDef W5500_BasicInit(const uint8_t mac[6], const uint8_t ip[4], const uint8_t gateway[4], const uint8_t netmask[4])
{
  uint8_t version = W5500_Read8(W5500_REG_VERSIONR, W5500_BSB_COMMON);

  APP_LOG_INFO("W5500 version=0x%02X", (unsigned int)version);
  if (version != 0x04u)
  {
    APP_LOG_ERR("W5500 unexpected version=0x%02X", (unsigned int)version);
    return HAL_ERROR;
  }

  W5500_Write(W5500_REG_GAR, W5500_BSB_COMMON, gateway, 4);
  W5500_Write(W5500_REG_SUBR, W5500_BSB_COMMON, netmask, 4);
  W5500_Write(W5500_REG_SHAR, W5500_BSB_COMMON, mac, 6);
  W5500_Write(W5500_REG_SIPR, W5500_BSB_COMMON, ip, 4);
  W5500_ClearSocketMemoryMap();
  APP_LOG_INFO("W5500 ip=%u.%u.%u.%u gw=%u.%u.%u.%u",
               (unsigned int)ip[0], (unsigned int)ip[1],
               (unsigned int)ip[2], (unsigned int)ip[3],
               (unsigned int)gateway[0], (unsigned int)gateway[1],
               (unsigned int)gateway[2], (unsigned int)gateway[3]);

  return HAL_OK;
}

uint8_t W5500_SocketStatus(uint8_t socket_id)
{
  if (!W5500_IsSocketValid(socket_id))
  {
    return W5500_SOCK_SR_CLOSED;
  }

  return W5500_Read8(W5500_SOCK_REG_SR, W5500_SocketRegBlock(socket_id));
}

HAL_StatusTypeDef W5500_SocketOpenTcpServer(uint8_t socket_id, uint16_t port, uint8_t tx_kb, uint8_t rx_kb)
{
  uint8_t block;

  if (!W5500_IsSocketValid(socket_id))
  {
    return HAL_ERROR;
  }

  block = W5500_SocketRegBlock(socket_id);
  if (tx_kb == 0u)
  {
    tx_kb = 2u;
  }
  if (rx_kb == 0u)
  {
    rx_kb = 2u;
  }

  s_tx_kb[socket_id] = tx_kb;
  s_rx_kb[socket_id] = rx_kb;

  (void)W5500_SocketCommand(socket_id, W5500_SOCK_CR_CLOSE);
  W5500_Write8(W5500_SOCK_REG_IR, block, W5500_SOCK_IR_ALL);
  W5500_Write8(W5500_SOCK_REG_MR, block, W5500_SOCK_MR_TCP);
  W5500_Write16(W5500_SOCK_REG_PORT, block, port);
  W5500_Write8(W5500_SOCK_REG_RXBUF, block, rx_kb);
  W5500_Write8(W5500_SOCK_REG_TXBUF, block, tx_kb);

  if (W5500_SocketCommand(socket_id, W5500_SOCK_CR_OPEN) != HAL_OK)
  {
    APP_LOG_ERR("W5500 socket%u open timeout", (unsigned int)socket_id);
    return HAL_TIMEOUT;
  }

  if (W5500_SocketStatus(socket_id) != W5500_SOCK_SR_INIT)
  {
    APP_LOG_ERR("W5500 socket%u open failed sr=0x%02X",
                (unsigned int)socket_id,
                (unsigned int)W5500_SocketStatus(socket_id));
    return HAL_ERROR;
  }

  if (W5500_SocketCommand(socket_id, W5500_SOCK_CR_LISTEN) != HAL_OK)
  {
    APP_LOG_ERR("W5500 socket%u listen timeout", (unsigned int)socket_id);
    return HAL_TIMEOUT;
  }

  APP_LOG_OK("W5500 socket%u listen port=%u tx=%uKB rx=%uKB",
             (unsigned int)socket_id,
             (unsigned int)port,
             (unsigned int)tx_kb,
             (unsigned int)rx_kb);
  return HAL_OK;
}

HAL_StatusTypeDef W5500_SocketServiceTcpServer(uint8_t socket_id, uint16_t port)
{
  uint8_t status = W5500_SocketStatus(socket_id);

  switch (status)
  {
    case W5500_SOCK_SR_CLOSED:
      return W5500_SocketOpenTcpServer(socket_id, port, s_tx_kb[socket_id], s_rx_kb[socket_id]);

    case W5500_SOCK_SR_INIT:
      return W5500_SocketCommand(socket_id, W5500_SOCK_CR_LISTEN);

    case W5500_SOCK_SR_LISTEN:
    case W5500_SOCK_SR_SYNSENT:
    case W5500_SOCK_SR_SYNRECV:
    case W5500_SOCK_SR_ESTABLISHED:
    case W5500_SOCK_SR_FIN_WAIT:
    case W5500_SOCK_SR_CLOSING:
    case W5500_SOCK_SR_TIME_WAIT:
    case W5500_SOCK_SR_LAST_ACK:
      return HAL_OK;

    case W5500_SOCK_SR_CLOSE_WAIT:
      (void)W5500_SocketCommand(socket_id, W5500_SOCK_CR_DISCON);
      (void)W5500_SocketCommand(socket_id, W5500_SOCK_CR_CLOSE);
      return HAL_BUSY;

    default:
      return HAL_BUSY;
  }
}

uint16_t W5500_SocketRxSize(uint8_t socket_id)
{
  uint16_t first;
  uint16_t second;
  uint8_t block;

  if (!W5500_IsSocketValid(socket_id))
  {
    return 0u;
  }

  block = W5500_SocketRegBlock(socket_id);
  do
  {
    first = W5500_Read16(W5500_SOCK_REG_RX_RSR, block);
    second = W5500_Read16(W5500_SOCK_REG_RX_RSR, block);
  } while (first != second);

  return first;
}

uint16_t W5500_SocketTxFree(uint8_t socket_id)
{
  uint16_t first;
  uint16_t second;
  uint8_t block;

  if (!W5500_IsSocketValid(socket_id))
  {
    return 0u;
  }

  block = W5500_SocketRegBlock(socket_id);
  do
  {
    first = W5500_Read16(W5500_SOCK_REG_TX_FSR, block);
    second = W5500_Read16(W5500_SOCK_REG_TX_FSR, block);
  } while (first != second);

  return first;
}

size_t W5500_SocketRecv(uint8_t socket_id, uint8_t *buffer, size_t len)
{
  uint16_t rx_size;
  uint16_t rx_rd;
  uint16_t offset;
  uint16_t mask;
  uint16_t first_len;
  uint8_t reg_block;
  uint8_t rx_block;

  if (!W5500_IsSocketValid(socket_id) || (buffer == NULL) || (len == 0u))
  {
    return 0u;
  }

  rx_size = W5500_SocketRxSize(socket_id);
  if (rx_size == 0u)
  {
    return 0u;
  }
  if (len > rx_size)
  {
    len = rx_size;
  }

  reg_block = W5500_SocketRegBlock(socket_id);
  rx_block = W5500_SocketRxBlock(socket_id);
  rx_rd = W5500_Read16(W5500_SOCK_REG_RX_RD, reg_block);
  mask = W5500_SocketMemMask(s_rx_kb[socket_id]);
  offset = (uint16_t)(rx_rd & mask);
  first_len = (uint16_t)((mask + 1u) - offset);
  if (first_len > len)
  {
    first_len = (uint16_t)len;
  }

  W5500_Read(offset, rx_block, buffer, first_len);
  if (first_len < len)
  {
    W5500_Read(0u, rx_block, &buffer[first_len], len - first_len);
  }

  W5500_Write16(W5500_SOCK_REG_RX_RD, reg_block, (uint16_t)(rx_rd + len));
  (void)W5500_SocketCommand(socket_id, W5500_SOCK_CR_RECV);
  return len;
}

size_t W5500_SocketSend(uint8_t socket_id, const uint8_t *buffer, size_t len)
{
  uint16_t free_size;
  uint16_t tx_wr;
  uint16_t offset;
  uint16_t mask;
  uint16_t first_len;
  uint8_t reg_block;
  uint8_t tx_block;

  if (!W5500_IsSocketValid(socket_id) || (buffer == NULL) || (len == 0u))
  {
    return 0u;
  }

  if (W5500_SocketStatus(socket_id) != W5500_SOCK_SR_ESTABLISHED)
  {
    return 0u;
  }

  free_size = W5500_SocketTxFree(socket_id);
  if (free_size < len)
  {
    return 0u;
  }

  reg_block = W5500_SocketRegBlock(socket_id);
  tx_block = W5500_SocketTxBlock(socket_id);
  tx_wr = W5500_Read16(W5500_SOCK_REG_TX_WR, reg_block);
  mask = W5500_SocketMemMask(s_tx_kb[socket_id]);
  offset = (uint16_t)(tx_wr & mask);
  first_len = (uint16_t)((mask + 1u) - offset);
  if (first_len > len)
  {
    first_len = (uint16_t)len;
  }

  W5500_Write(offset, tx_block, buffer, first_len);
  if (first_len < len)
  {
    W5500_Write(0u, tx_block, &buffer[first_len], len - first_len);
  }

  W5500_Write16(W5500_SOCK_REG_TX_WR, reg_block, (uint16_t)(tx_wr + len));
  if (W5500_SocketCommand(socket_id, W5500_SOCK_CR_SEND) != HAL_OK)
  {
    return 0u;
  }

  return len;
}
