#ifndef W25Q64_H
#define W25Q64_H

#include <stdbool.h>
#include <stdint.h>
#include "stm32f1xx_hal.h"

#define W25Q64_TOTAL_SIZE_BYTES  (8UL * 1024UL * 1024UL)
#define W25Q64_SECTOR_SIZE       4096UL
#define W25Q64_PAGE_SIZE         256UL

typedef struct {
  uint8_t manufacturer_id;
  uint8_t memory_type;
  uint8_t capacity_id;
} W25Q64_Info_t;

HAL_StatusTypeDef W25Q64_Init(SPI_HandleTypeDef *spi);
HAL_StatusTypeDef W25Q64_GetInfo(W25Q64_Info_t *info);
HAL_StatusTypeDef W25Q64_Read(uint32_t address, uint8_t *data, uint32_t length);
HAL_StatusTypeDef W25Q64_Program(uint32_t address, const uint8_t *data,
                                uint32_t length);
HAL_StatusTypeDef W25Q64_EraseSector(uint32_t address);
bool W25Q64_IsReady(void);

#endif
