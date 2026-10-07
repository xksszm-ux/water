#include <string.h>
#include "../../stm32/Drivers/MPU6050/mpu6050.c"
#include "../../stm32/Drivers/OLED/oled.c"
#include "../../stm32/Drivers/W25Q64/w25q64.c"
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
GPIO_TypeDef mock_gpio_b;
static I2C_HandleTypeDef i2c;
static SPI_HandleTypeDef spi;
static unsigned calls, fail_at, address_fail, tx_packets, program_count, read_count;
static uint32_t tick, last_address, addresses[4];
static uint16_t lengths[4];
static uint8_t identity=0x68, opcode, status, jedec_capacity=0x17;
static bool cs_active, bus_violation, ignore_write_enable, busy_after_write;
osStatus_t osDelay(uint32_t n) {tick+=n;return osOK;}
uint32_t HAL_GetTick(void) {return tick;}
static HAL_StatusTypeDef BusResult(void) {return ++calls==fail_at ? HAL_TIMEOUT:HAL_OK;}
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *h,uint16_t a,uint32_t n,uint32_t t) {
  (void)h;(void)n;(void)t;
  return address_fail==2 || (address_fail==1 && (a==0xd0 || a==0x78)) ? HAL_ERROR:HAL_OK;
}
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *h,uint16_t a,uint16_t r,uint16_t s,uint8_t *p,uint16_t n,uint32_t t) {
  (void)h;(void)a;(void)r;(void)s;(void)p;(void)n;(void)t;return BusResult();
}
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *h,uint16_t a,uint16_t r,uint16_t s,uint8_t *p,uint16_t n,uint32_t t) {
  (void)h;(void)a;(void)s;(void)t;
  HAL_StatusTypeDef rc=BusResult(); if(rc!=HAL_OK)return rc;
  memset(p,0,n);if(r==0x75) p[0]=identity;
  else if(n==14) {p[0]=0x40;p[2]=0xc0;p[8]=0x00;p[9]=0x83;p[10]=0xff;p[11]=0x7d;}
  return HAL_OK;
}
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *h,uint16_t a,uint8_t *p,uint16_t n,uint32_t t) {
  (void)h;(void)a;(void)t;
  if(!((n==2 && p[0]==0) || (n==17 && p[0]==0x40)))bus_violation=true;
  ++tx_packets;return BusResult();
}
void HAL_GPIO_WritePin(GPIO_TypeDef *p,uint16_t pin,GPIO_PinState s) {
  (void)p;(void)pin;cs_active=s==GPIO_PIN_RESET;
  if(cs_active)opcode=0;
}
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *h,uint8_t *p,uint16_t n,uint32_t t) {
  (void)h;(void)t;if(!cs_active)bus_violation=true;
  HAL_StatusTypeDef rc=BusResult();if(rc!=HAL_OK)return rc;
  if(!opcode) {
    opcode=p[0];
    if(opcode==CMD_WRITE_ENABLE && !ignore_write_enable) status|=STATUS_WEL_MASK;
    if(n==4) {
      last_address=((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
      if(opcode==CMD_PAGE_PROGRAM && program_count<4) addresses[program_count]=last_address;
      if(opcode==CMD_SECTOR_ERASE && busy_after_write)status|=STATUS_BUSY_MASK;
    }
  } else if(opcode==CMD_PAGE_PROGRAM && program_count<4) {
    lengths[program_count++]=n;status &= (uint8_t)~STATUS_WEL_MASK;
    if(busy_after_write)status|=STATUS_BUSY_MASK;
  }
  return HAL_OK;
}
HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef *h,uint8_t *p,uint16_t n,uint32_t t) {
  (void)h;(void)t;if(!cs_active)bus_violation=true;
  HAL_StatusTypeDef rc=BusResult();if(rc!=HAL_OK)return rc;
  memset(p,0,n);
  if(opcode==CMD_JEDEC_ID && n==3) {p[0]=0xef;p[1]=0x40;p[2]=jedec_capacity;}
  else if(opcode==CMD_READ_STATUS_1) p[0]=status;
  else if(opcode==CMD_READ_DATA && read_count<4) {addresses[read_count]=last_address;lengths[read_count++]=n;}
  return HAL_OK;
}
static void ResetBus(void) {
  calls=fail_at=address_fail=tx_packets=program_count=read_count=0;tick=0;
  identity=0x68;jedec_capacity=0x17;opcode=status=0;cs_active=bus_violation=ignore_write_enable=busy_after_write=false;
  memset(addresses,0,sizeof(addresses));memset(lengths,0,sizeof(lengths));
}
int I2cSpiChecks_Run(void) {
  Mpu6050Data_t data;
  ResetBus(); CHECK(MPU6050_Init(&i2c)==HAL_OK);
  CHECK(MPU6050_Read(&data)==HAL_OK && data.accel_mg[0]==1000 && data.accel_mg[1]==-1000 && data.gyro_mdps[0]==2000 && data.gyro_mdps[1]==-2000);
  CHECK(MPU6050_Init(NULL)==HAL_ERROR && !MPU6050_IsReady());
  for(unsigned f=1;f<=7;++f) {ResetBus();fail_at=f;CHECK(MPU6050_Init(&i2c)!=HAL_OK && !MPU6050_IsReady());CHECK(MPU6050_Read(&data)==HAL_ERROR);}
  ResetBus();address_fail=1;CHECK(MPU6050_Init(&i2c)==HAL_OK && MPU6050_GetAddress()==0x69);
  ResetBus();address_fail=2;CHECK(MPU6050_Init(&i2c)==HAL_ERROR);
  ResetBus();identity=0xff;CHECK(MPU6050_Init(&i2c)==HAL_ERROR);
  ResetBus();CHECK(MPU6050_Init(&i2c)==HAL_OK);fail_at=calls+1;
  CHECK(MPU6050_Read(&data)==HAL_ERROR);fail_at=0;CHECK(MPU6050_Init(&i2c)==HAL_OK);
  for(unsigned f=1;f<=25;++f) {ResetBus();fail_at=f;CHECK(OLED_Init(&i2c)!=HAL_OK && !OLED_IsReady());}
  ResetBus();address_fail=1;CHECK(OLED_Init(&i2c)==HAL_OK && OLED_GetAddress()==0x3d);
  CHECK(OLED_Init(NULL)==HAL_ERROR && !OLED_IsReady());
  for(unsigned f=1;f<=11;++f) {
    ResetBus();CHECK(OLED_Init(&i2c)==HAL_OK);fail_at=calls+f;
    CHECK(OLED_UpdatePage(0)==HAL_ERROR && !OLED_IsReady() && !bus_violation);
    fail_at=0;CHECK(OLED_Init(&i2c)==HAL_OK && OLED_UpdatePage(7)==HAL_OK);
  }
  CHECK(OLED_UpdatePage(8)==HAL_ERROR);OLED_SetCursor(126,0);OLED_WriteString("A");
  for(unsigned f=1;f<=4;++f) {
    ResetBus();fail_at=f;CHECK(W25Q64_Init(&spi)!=HAL_OK && !W25Q64_IsReady() && !cs_active);
  }
  ResetBus();jedec_capacity=0;CHECK(W25Q64_Init(&spi)==HAL_ERROR && !flash_ready && !cs_active);
  ResetBus();status=STATUS_BUSY_MASK;tick=UINT32_MAX-50;
  CHECK(W25Q64_Init(&spi)==HAL_TIMEOUT && !flash_ready && !cs_active);
  static uint8_t bytes[4100];
  ResetBus();CHECK(W25Q64_Init(&spi)==HAL_OK);
  CHECK(W25Q64_Read(W25Q64_TOTAL_SIZE_BYTES-1,bytes,2)==HAL_ERROR);
  CHECK(W25Q64_Read(0x100,bytes,sizeof(bytes))==HAL_OK && read_count==2);
  CHECK(lengths[0]==4096 && lengths[1]==4 && addresses[1]==0x1100);
  program_count=0;CHECK(W25Q64_Program(255,bytes,258)==HAL_OK && program_count==3);
  CHECK(addresses[0]==255 && addresses[1]==256 && addresses[2]==512 && lengths[0]==1 && lengths[1]==256 && lengths[2]==1);
  CHECK(W25Q64_EraseSector(0x1234)==HAL_OK && last_address==0x1000 && !cs_active && !bus_violation);
  CHECK(W25Q64_Read(0,bytes,UINT32_MAX)==HAL_ERROR && W25Q64_Program(0,NULL,1)==HAL_ERROR);
  ResetBus();CHECK(W25Q64_Init(&spi)==HAL_OK);ignore_write_enable=true;
  CHECK(W25Q64_Program(0,bytes,1)==HAL_ERROR && !flash_ready && !cs_active);
  for(unsigned erase=0;erase<2;++erase) {
    ResetBus();CHECK(W25Q64_Init(&spi)==HAL_OK);busy_after_write=true;tick=UINT32_MAX-50;
    CHECK((erase ? W25Q64_EraseSector(0):W25Q64_Program(0,bytes,1))!=HAL_OK && !flash_ready && !cs_active);
    CHECK((uint32_t)(tick-(UINT32_MAX-50))==(erase ? 3000U:100U));
  }
  /* Inject every transaction boundary of one-page program and sector erase. */
  for(unsigned operation=0;operation<3;++operation) {
    ResetBus();CHECK(W25Q64_Init(&spi)==HAL_OK);calls=0;
    CHECK((operation==0 ? W25Q64_Program(0,bytes,2):operation==1 ? W25Q64_EraseSector(0):W25Q64_Read(0,bytes,2))==HAL_OK);
    unsigned total=calls;
    for(unsigned f=1;f<=total;++f) {
      ResetBus();CHECK(W25Q64_Init(&spi)==HAL_OK);calls=0;fail_at=f;
      CHECK((operation==0 ? W25Q64_Program(0,bytes,2):operation==1 ? W25Q64_EraseSector(0):W25Q64_Read(0,bytes,2))!=HAL_OK);
      CHECK(!W25Q64_IsReady() && !cs_active && !bus_violation);
      fail_at=0;CHECK(W25Q64_Init(&spi)==HAL_OK);
    }
  }
  return 0;
}
