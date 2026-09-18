#include "w25q64.h"
#include "main.h"

#define CMD_WRITE_ENABLE   0x06U
#define CMD_READ_STATUS_1  0x05U
#define CMD_READ_DATA      0x03U
#define CMD_PAGE_PROGRAM   0x02U
#define CMD_SECTOR_ERASE   0x20U
#define CMD_JEDEC_ID       0x9FU

#define SPI_TIMEOUT_MS        100U
#define PROGRAM_TIMEOUT_MS    100U
#define ERASE_TIMEOUT_MS     3000U
#define STATUS_BUSY_MASK      0x01U
#define STATUS_WEL_MASK       0x02U

static SPI_HandleTypeDef *flash_spi;
static W25Q64_Info_t flash_info;
static bool flash_ready;

static void Select(void)
{
  HAL_GPIO_WritePin(W25Q64_CS_GPIO_Port, W25Q64_CS_Pin, GPIO_PIN_RESET);
}

static void Deselect(void)
{
  HAL_GPIO_WritePin(W25Q64_CS_GPIO_Port, W25Q64_CS_Pin, GPIO_PIN_SET);
}

static HAL_StatusTypeDef ReadStatus(uint8_t *status)
{
  const uint8_t command = CMD_READ_STATUS_1;
  HAL_StatusTypeDef result;
  Select();
  result = HAL_SPI_Transmit(flash_spi, (uint8_t *)&command, 1U, SPI_TIMEOUT_MS);
  if (result == HAL_OK) {
    result = HAL_SPI_Receive(flash_spi, status, 1U, SPI_TIMEOUT_MS);
  }
  Deselect();
  if (result != HAL_OK) flash_ready = false;
  return result;
}

static HAL_StatusTypeDef WaitWhileBusy(uint32_t timeout_ms)
{
  const uint32_t started_at = HAL_GetTick();
  uint8_t status;
  do {
    if (ReadStatus(&status) != HAL_OK) return HAL_ERROR;
    if ((status & STATUS_BUSY_MASK) == 0U) return HAL_OK;
    HAL_Delay(1U);
  } while ((uint32_t)(HAL_GetTick() - started_at) < timeout_ms);
  flash_ready = false;
  return HAL_TIMEOUT;
}

static HAL_StatusTypeDef WriteEnable(void)
{
  const uint8_t command = CMD_WRITE_ENABLE;
  uint8_t status;
  Select();
  const HAL_StatusTypeDef result =
      HAL_SPI_Transmit(flash_spi, (uint8_t *)&command, 1U, SPI_TIMEOUT_MS);
  Deselect();
  if ((result != HAL_OK) || (ReadStatus(&status) != HAL_OK) ||
      ((status & STATUS_WEL_MASK) == 0U)) {
    flash_ready = false;
    return HAL_ERROR;
  }
  return HAL_OK;
}

static void BuildAddressCommand(uint8_t command[4], uint8_t opcode,
                                uint32_t address)
{
  command[0] = opcode;
  command[1] = (uint8_t)(address >> 16U);
  command[2] = (uint8_t)(address >> 8U);
  command[3] = (uint8_t)address;
}

HAL_StatusTypeDef W25Q64_Init(SPI_HandleTypeDef *spi)
{
  const uint8_t command = CMD_JEDEC_ID;
  uint8_t id[3] = {0U};
  flash_ready = false;
  if (spi == NULL) return HAL_ERROR;
  flash_spi = spi;
  Deselect();
  HAL_Delay(1U);
  Select();
  HAL_StatusTypeDef result =
      HAL_SPI_Transmit(flash_spi, (uint8_t *)&command, 1U, SPI_TIMEOUT_MS);
  if (result == HAL_OK) {
    result = HAL_SPI_Receive(flash_spi, id, sizeof(id), SPI_TIMEOUT_MS);
  }
  Deselect();
  if (result != HAL_OK) return result;
  flash_info.manufacturer_id = id[0];
  flash_info.memory_type = id[1];
  flash_info.capacity_id = id[2];
  if ((id[0] != 0xEFU) || (id[1] != 0x40U) || (id[2] != 0x17U)) {
    return HAL_ERROR;
  }
  flash_ready = true;
  const HAL_StatusTypeDef ready_result = WaitWhileBusy(SPI_TIMEOUT_MS);
  return ready_result;
}

HAL_StatusTypeDef W25Q64_GetInfo(W25Q64_Info_t *info)
{
  if (!flash_ready || (info == NULL)) return HAL_ERROR;
  *info = flash_info;
  return HAL_OK;
}

HAL_StatusTypeDef W25Q64_Read(uint32_t address, uint8_t *data, uint32_t length)
{
  uint8_t command[4];
  if (!flash_ready || (data == NULL) || (length == 0U) ||
      (address >= W25Q64_TOTAL_SIZE_BYTES) ||
      (length > (W25Q64_TOTAL_SIZE_BYTES - address))) return HAL_ERROR;
  while (length > 0U) {
    const uint16_t chunk = length > 4096U ? 4096U : (uint16_t)length;
    BuildAddressCommand(command, CMD_READ_DATA, address);
    Select();
    HAL_StatusTypeDef result =
        HAL_SPI_Transmit(flash_spi, command, sizeof(command), SPI_TIMEOUT_MS);
    if (result == HAL_OK) {
      result = HAL_SPI_Receive(flash_spi, data, chunk, SPI_TIMEOUT_MS);
    }
    Deselect();
    if (result != HAL_OK) {
      flash_ready = false;
      return result;
    }
    address += chunk;
    data += chunk;
    length -= chunk;
  }
  return HAL_OK;
}

HAL_StatusTypeDef W25Q64_Program(uint32_t address, const uint8_t *data,
                                uint32_t length)
{
  uint8_t command[4];
  if (!flash_ready || (data == NULL) || (length == 0U) ||
      (address >= W25Q64_TOTAL_SIZE_BYTES) ||
      (length > (W25Q64_TOTAL_SIZE_BYTES - address))) return HAL_ERROR;
  while (length > 0U) {
    const uint32_t page_remaining = W25Q64_PAGE_SIZE -
                                    (address % W25Q64_PAGE_SIZE);
    const uint16_t chunk =
        (uint16_t)(length < page_remaining ? length : page_remaining);
    if ((WaitWhileBusy(PROGRAM_TIMEOUT_MS) != HAL_OK) ||
        (WriteEnable() != HAL_OK)) {
      flash_ready = false;
      return HAL_ERROR;
    }
    BuildAddressCommand(command, CMD_PAGE_PROGRAM, address);
    Select();
    HAL_StatusTypeDef result =
        HAL_SPI_Transmit(flash_spi, command, sizeof(command), SPI_TIMEOUT_MS);
    if (result == HAL_OK) {
      result = HAL_SPI_Transmit(flash_spi, (uint8_t *)data, chunk, SPI_TIMEOUT_MS);
    }
    Deselect();
    if ((result != HAL_OK) || (WaitWhileBusy(PROGRAM_TIMEOUT_MS) != HAL_OK)) {
      flash_ready = false;
      return HAL_ERROR;
    }
    address += chunk;
    data += chunk;
    length -= chunk;
  }
  return HAL_OK;
}

HAL_StatusTypeDef W25Q64_EraseSector(uint32_t address)
{
  uint8_t command[4];
  if (!flash_ready || (address >= W25Q64_TOTAL_SIZE_BYTES)) return HAL_ERROR;
  address -= address % W25Q64_SECTOR_SIZE;
  if ((WaitWhileBusy(PROGRAM_TIMEOUT_MS) != HAL_OK) ||
      (WriteEnable() != HAL_OK)) {
    flash_ready = false;
    return HAL_ERROR;
  }
  BuildAddressCommand(command, CMD_SECTOR_ERASE, address);
  Select();
  const HAL_StatusTypeDef result =
      HAL_SPI_Transmit(flash_spi, command, sizeof(command), SPI_TIMEOUT_MS);
  Deselect();
  if (result != HAL_OK) {
    flash_ready = false;
    return result;
  }
  return WaitWhileBusy(ERASE_TIMEOUT_MS);
}

bool W25Q64_IsReady(void)
{
  return flash_ready;
}
