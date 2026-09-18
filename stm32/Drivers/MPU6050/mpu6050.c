#include "mpu6050.h"

#define MPU6050_ADDRESS_LOW       0x68U
#define MPU6050_ADDRESS_HIGH      0x69U
#define MPU6050_REG_SMPLRT_DIV    0x19U
#define MPU6050_REG_CONFIG        0x1AU
#define MPU6050_REG_GYRO_CONFIG   0x1BU
#define MPU6050_REG_ACCEL_CONFIG  0x1CU
#define MPU6050_REG_ACCEL_XOUT_H  0x3BU
#define MPU6050_REG_PWR_MGMT_1    0x6BU
#define MPU6050_REG_WHO_AM_I      0x75U
#define MPU6050_TIMEOUT_MS        20U

static I2C_HandleTypeDef *mpu_i2c;
static uint8_t mpu_address;
static bool mpu_ready;

static uint16_t DeviceAddress(void)
{
  return (uint16_t)(mpu_address << 1U);
}

static HAL_StatusTypeDef WriteRegister(uint8_t reg, uint8_t value)
{
  return HAL_I2C_Mem_Write(mpu_i2c, DeviceAddress(), reg,
                           I2C_MEMADD_SIZE_8BIT, &value, 1U,
                           MPU6050_TIMEOUT_MS);
}

static HAL_StatusTypeDef ReadRegisters(uint8_t reg, uint8_t *data, uint16_t size)
{
  return HAL_I2C_Mem_Read(mpu_i2c, DeviceAddress(), reg,
                          I2C_MEMADD_SIZE_8BIT, data, size,
                          MPU6050_TIMEOUT_MS);
}

static int16_t ReadBe16(const uint8_t *data)
{
  return (int16_t)(((uint16_t)data[0] << 8U) | data[1]);
}

HAL_StatusTypeDef MPU6050_Init(I2C_HandleTypeDef *i2c)
{
  if (i2c == NULL) return HAL_ERROR;
  mpu_i2c = i2c;
  mpu_ready = false;

  const uint8_t addresses[] = {MPU6050_ADDRESS_LOW, MPU6050_ADDRESS_HIGH};
  for (uint32_t index = 0U; index < 2U; ++index) {
    mpu_address = addresses[index];
    if (HAL_I2C_IsDeviceReady(mpu_i2c, DeviceAddress(), 2U,
                              MPU6050_TIMEOUT_MS) == HAL_OK) {
      break;
    }
    mpu_address = 0U;
  }
  if (mpu_address == 0U) return HAL_ERROR;

  uint8_t who_am_i = 0U;
  if ((ReadRegisters(MPU6050_REG_WHO_AM_I, &who_am_i, 1U) != HAL_OK) ||
      ((who_am_i != MPU6050_ADDRESS_LOW) &&
       (who_am_i != MPU6050_ADDRESS_HIGH))) {
    return HAL_ERROR;
  }

  if (WriteRegister(MPU6050_REG_PWR_MGMT_1, 0x80U) != HAL_OK) return HAL_ERROR;
  HAL_Delay(100U);
  if (WriteRegister(MPU6050_REG_PWR_MGMT_1, 0x01U) != HAL_OK) return HAL_ERROR;
  if (WriteRegister(MPU6050_REG_SMPLRT_DIV, 4U) != HAL_OK) return HAL_ERROR;
  if (WriteRegister(MPU6050_REG_CONFIG, 0x03U) != HAL_OK) return HAL_ERROR;
  if (WriteRegister(MPU6050_REG_GYRO_CONFIG, 0x08U) != HAL_OK) return HAL_ERROR;
  if (WriteRegister(MPU6050_REG_ACCEL_CONFIG, 0x00U) != HAL_OK) return HAL_ERROR;

  mpu_ready = true;
  return HAL_OK;
}

HAL_StatusTypeDef MPU6050_Read(Mpu6050Data_t *data)
{
  if (!mpu_ready || (data == NULL)) return HAL_ERROR;

  uint8_t raw[14];
  if (ReadRegisters(MPU6050_REG_ACCEL_XOUT_H, raw, sizeof(raw)) != HAL_OK) {
    return HAL_ERROR;
  }

  const int16_t accel_x = ReadBe16(&raw[0]);
  const int16_t accel_y = ReadBe16(&raw[2]);
  const int16_t accel_z = ReadBe16(&raw[4]);
  const int16_t temperature = ReadBe16(&raw[6]);
  const int16_t gyro_x = ReadBe16(&raw[8]);
  const int16_t gyro_y = ReadBe16(&raw[10]);
  const int16_t gyro_z = ReadBe16(&raw[12]);

  data->accel_mg[0] = (int16_t)(((int32_t)accel_x * 1000) / 16384);
  data->accel_mg[1] = (int16_t)(((int32_t)accel_y * 1000) / 16384);
  data->accel_mg[2] = (int16_t)(((int32_t)accel_z * 1000) / 16384);
  data->gyro_mdps[0] = ((int32_t)gyro_x * 2000) / 131;
  data->gyro_mdps[1] = ((int32_t)gyro_y * 2000) / 131;
  data->gyro_mdps[2] = ((int32_t)gyro_z * 2000) / 131;
  data->temperature_centi_c =
      (int16_t)(3653 + (((int32_t)temperature * 100) / 340));
  return HAL_OK;
}

bool MPU6050_IsReady(void)
{
  return mpu_ready;
}

uint8_t MPU6050_GetAddress(void)
{
  return mpu_address;
}
