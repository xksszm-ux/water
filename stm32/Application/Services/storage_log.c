#include "storage_log.h"
#include "w25q64.h"
#include <stdbool.h>
#include <string.h>

#define LOG_MAGIC          0x524C4F47UL
#define LOG_START_ADDRESS  0x00001000UL
#define LOG_REGION_SIZE    (512UL * 1024UL)
#define LOG_END_ADDRESS    (LOG_START_ADDRESS + LOG_REGION_SIZE)
#define LOG_RECORD_SIZE    32UL
#define LOG_SCAN_BLOCK     256UL
#define LOG_FORMAT_VERSION 1U
#define LOG_SECTOR_COUNT   (LOG_REGION_SIZE / W25Q64_SECTOR_SIZE)

static uint8_t scan_buffer[LOG_SCAN_BLOCK];
static uint32_t next_address;
static uint32_t next_sequence;
static bool log_initialized;
volatile uint32_t g_storage_record_count;

static uint16_t ReadU16(const uint8_t *data)
{
  return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t ReadU32(const uint8_t *data)
{
  return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
         ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static void WriteU16(uint8_t *data, uint16_t value)
{
  data[0] = (uint8_t)value;
  data[1] = (uint8_t)(value >> 8U);
}

static void WriteU32(uint8_t *data, uint32_t value)
{
  data[0] = (uint8_t)value;
  data[1] = (uint8_t)(value >> 8U);
  data[2] = (uint8_t)(value >> 16U);
  data[3] = (uint8_t)(value >> 24U);
}

static uint16_t Crc16(const uint8_t *data, uint32_t length)
{
  uint16_t crc = 0xFFFFU;
  while (length-- > 0U) {
    crc ^= (uint16_t)(*data++) << 8U;
    for (uint8_t bit = 0U; bit < 8U; ++bit) {
      crc = (crc & 0x8000U) != 0U ?
            (uint16_t)((crc << 1U) ^ 0x1021U) : (uint16_t)(crc << 1U);
    }
  }
  return crc;
}

static bool RecordValid(const uint8_t record[LOG_RECORD_SIZE])
{
  return (ReadU32(record) == LOG_MAGIC) &&
         ((record[23] & 0x7FU) == LOG_FORMAT_VERSION) &&
         (ReadU16(&record[30]) == Crc16(record, 30U));
}

static bool IsErased(const uint8_t *data, uint32_t length)
{
  while (length-- > 0U) {
    if (*data++ != 0xFFU) return false;
  }
  return true;
}

static bool AddressValid(uint32_t address)
{
  return (address >= LOG_START_ADDRESS) && (address < LOG_END_ADDRESS) &&
         (((address - LOG_START_ADDRESS) % LOG_RECORD_SIZE) == 0U);
}

static HAL_StatusTypeDef InspectSector(uint32_t sector_address,
                                       bool *erased,
                                       uint32_t *valid_records)
{
  if ((erased == NULL) || (valid_records == NULL) ||
      !AddressValid(sector_address) ||
      ((sector_address % W25Q64_SECTOR_SIZE) != 0U)) return HAL_ERROR;
  *erased = true;
  *valid_records = 0U;
  for (uint32_t offset = 0U; offset < W25Q64_SECTOR_SIZE;
       offset += LOG_SCAN_BLOCK) {
    if (W25Q64_Read(sector_address + offset, scan_buffer,
                    sizeof(scan_buffer)) != HAL_OK) return HAL_ERROR;
    if (!IsErased(scan_buffer, sizeof(scan_buffer))) *erased = false;
    for (uint32_t record_offset = 0U; record_offset < LOG_SCAN_BLOCK;
         record_offset += LOG_RECORD_SIZE) {
      if (RecordValid(&scan_buffer[record_offset])) ++(*valid_records);
    }
  }
  return HAL_OK;
}

static HAL_StatusTypeDef PrepareSector(uint32_t sector_address)
{
  bool erased;
  uint32_t valid_records;
  if (InspectSector(sector_address, &erased, &valid_records) != HAL_OK) {
    return HAL_ERROR;
  }
  if (!erased) {
    if (W25Q64_EraseSector(sector_address) != HAL_OK) return HAL_ERROR;
    if (valid_records >= g_storage_record_count) g_storage_record_count = 0U;
    else g_storage_record_count -= valid_records;
    if (InspectSector(sector_address, &erased, &valid_records) != HAL_OK ||
        !erased) return HAL_ERROR;
  }
  return HAL_OK;
}

HAL_StatusTypeDef StorageLog_Init(void)
{
  bool found = false;
  uint32_t newest_sequence = 0U;
  uint32_t newest_address = LOG_START_ADDRESS;
  log_initialized = false;
  g_storage_record_count = 0U;
  for (uint32_t address = LOG_START_ADDRESS; address < LOG_END_ADDRESS;
       address += LOG_SCAN_BLOCK) {
    if (W25Q64_Read(address, scan_buffer, sizeof(scan_buffer)) != HAL_OK) {
      return HAL_ERROR;
    }
    for (uint32_t offset = 0U; offset < LOG_SCAN_BLOCK;
         offset += LOG_RECORD_SIZE) {
      const uint8_t *record = &scan_buffer[offset];
      if (RecordValid(record)) {
        const uint32_t sequence = ReadU32(&record[4]);
        ++g_storage_record_count;
        if (!found || ((int32_t)(sequence - newest_sequence) > 0)) {
          found = true;
          newest_sequence = sequence;
          newest_address = address + offset;
        }
      }
    }
  }
  if (!found) {
    next_address = LOG_START_ADDRESS;
    next_sequence = 0U;
  } else {
    next_address = newest_address + LOG_RECORD_SIZE;
    if (next_address >= LOG_END_ADDRESS) next_address = LOG_START_ADDRESS;
    next_sequence = newest_sequence + 1U;
  }
  log_initialized = true;
  return HAL_OK;
}

HAL_StatusTypeDef StorageLog_Append(const RobotStatus_t *status)
{
  uint8_t record[LOG_RECORD_SIZE];
  uint8_t existing[LOG_RECORD_SIZE];
  if ((status == NULL) || !log_initialized || !AddressValid(next_address)) {
    return HAL_ERROR;
  }
  if ((next_address % W25Q64_SECTOR_SIZE) == 0U) {
    if (PrepareSector(next_address) != HAL_OK) return HAL_ERROR;
  }
  if (W25Q64_Read(next_address, existing, sizeof(existing)) != HAL_OK) {
    return HAL_ERROR;
  }
  if (!IsErased(existing, sizeof(existing))) {
    const uint32_t sector = ((next_address - LOG_START_ADDRESS) /
                             W25Q64_SECTOR_SIZE + 1U) % LOG_SECTOR_COUNT;
    next_address = LOG_START_ADDRESS + sector * W25Q64_SECTOR_SIZE;
    if (PrepareSector(next_address) != HAL_OK) return HAL_ERROR;
  }

  memset(record, 0, sizeof(record));
  WriteU32(&record[0], LOG_MAGIC);
  WriteU32(&record[4], next_sequence);
  WriteU32(&record[8], status->updated_at_ms);
  WriteU16(&record[12], status->battery_mv);
  WriteU16(&record[14], status->distance_mm);
  WriteU16(&record[16], (uint16_t)status->left_output_permille);
  WriteU16(&record[18], (uint16_t)status->right_output_permille);
  record[20] = (uint8_t)status->mode;
  record[21] = status->error_status;
  record[22] = status->sensor_valid_mask;
  record[23] = (uint8_t)(LOG_FORMAT_VERSION |
                         (status->battery_valid ? 0x80U : 0x00U));
  WriteU16(&record[24], (uint16_t)status->accel_mg[0]);
  WriteU16(&record[26], (uint16_t)status->accel_mg[1]);
  WriteU16(&record[28], (uint16_t)status->accel_mg[2]);
  WriteU16(&record[30], Crc16(record, 30U));
  if (W25Q64_Program(next_address, record, sizeof(record)) != HAL_OK) {
    return HAL_ERROR;
  }
  if ((W25Q64_Read(next_address, existing, sizeof(existing)) != HAL_OK) ||
      (memcmp(record, existing, sizeof(record)) != 0) ||
      !RecordValid(existing)) return HAL_ERROR;
  ++next_sequence;
  ++g_storage_record_count;
  next_address += LOG_RECORD_SIZE;
  if (next_address >= LOG_END_ADDRESS) next_address = LOG_START_ADDRESS;
  return HAL_OK;
}

uint32_t StorageLog_GetRecordCount(void)
{
  return g_storage_record_count;
}
