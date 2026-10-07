/* Production log format/scanner/writer. Byte array replaces NOR operations;
   partial writes are injected scenarios, not observed hardware power cuts. */
#include "../../stm32/Application/Services/Storage/storage_log.c"
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static uint8_t flash[LOG_END_ADDRESS];
static bool read_fail, program_partial, erase_fail, erase_incomplete;
HAL_StatusTypeDef W25Q64_Read(uint32_t a,uint8_t *d,uint32_t n) {
  if(read_fail || a>sizeof(flash) || n>sizeof(flash)-a) return HAL_ERROR;
  memcpy(d,&flash[a],n); return HAL_OK;
}
HAL_StatusTypeDef W25Q64_Program(uint32_t a,const uint8_t *d,uint32_t n) {
  if(a>sizeof(flash) || n>sizeof(flash)-a) return HAL_ERROR;
  if(program_partial) n=8;
  for(uint32_t i=0;i<n;++i) flash[a+i]&=d[i];
  return program_partial ? HAL_ERROR : HAL_OK;
}
HAL_StatusTypeDef W25Q64_EraseSector(uint32_t a) {
  if(erase_fail || a%W25Q64_SECTOR_SIZE || a>sizeof(flash)-W25Q64_SECTOR_SIZE) return HAL_ERROR;
  if(!erase_incomplete) memset(&flash[a],255,W25Q64_SECTOR_SIZE);
  return HAL_OK;
}
int StorageLogChecks_Run(void) {
  RobotStatus_t status={.updated_at_ms=42,.battery_mv=4500,.battery_valid=true,
    .distance_mm=123,.sensor_valid_mask=SENSOR_VALID_DISTANCE,.left_output_permille=-20};
  memset(flash,255,sizeof(flash));
  CHECK(StorageLog_Init()==HAL_OK && StorageLog_GetRecordCount()==0);
  CHECK(StorageLog_Append(&status)==HAL_OK && StorageLog_GetRecordCount()==1);
  CHECK(RecordValid(&flash[LOG_START_ADDRESS]));
  CHECK(ReadU32(&flash[LOG_START_ADDRESS+8])==42 && ReadU16(&flash[LOG_START_ADDRESS+16])==(uint16_t)-20);
  CHECK(StorageLog_Init()==HAL_OK && next_sequence==1 && StorageLog_GetRecordCount()==1);
  program_partial=true; CHECK(StorageLog_Append(&status)==HAL_ERROR && StorageLog_GetRecordCount()==1);
  program_partial=false;
  CHECK(StorageLog_Init()==HAL_OK && StorageLog_GetRecordCount()==1);
  CHECK(StorageLog_Append(&status)==HAL_OK && StorageLog_GetRecordCount()==2);
  CHECK(RecordValid(&flash[LOG_START_ADDRESS])); /* committed first record survived */
  CHECK(RecordValid(&flash[LOG_START_ADDRESS+W25Q64_SECTOR_SIZE])); /* torn slot skipped */
  flash[LOG_START_ADDRESS+12]^=1; /* detected corruption, no copied CRC routine */
  CHECK(StorageLog_Init()==HAL_OK && StorageLog_GetRecordCount()==1);
  read_fail=true; CHECK(StorageLog_Init()==HAL_ERROR && StorageLog_Append(&status)==HAL_ERROR);
  read_fail=false;
  memset(flash,255,sizeof(flash));
  CHECK(StorageLog_Init()==HAL_OK);
  next_sequence=UINT32_MAX;
  CHECK(StorageLog_Append(&status)==HAL_OK && StorageLog_Append(&status)==HAL_OK);
  CHECK(StorageLog_Init()==HAL_OK && next_sequence==1 && StorageLog_GetRecordCount()==2);
  /* Full ring overwrites one complete sector, never beyond the log region. */
  memset(flash,255,sizeof(flash)); CHECK(StorageLog_Init()==HAL_OK);
  for(unsigned i=0;i<LOG_REGION_SIZE/LOG_RECORD_SIZE;++i) CHECK(StorageLog_Append(&status)==HAL_OK);
  CHECK(next_address==LOG_START_ADDRESS && StorageLog_GetRecordCount()==16384);
  CHECK(IsErased(flash,LOG_START_ADDRESS)); /* reserved prefix untouched */
  erase_fail=true; CHECK(StorageLog_Append(&status)==HAL_ERROR && StorageLog_GetRecordCount()==16384);
  erase_fail=false; erase_incomplete=true; CHECK(StorageLog_Append(&status)==HAL_ERROR);
  erase_incomplete=false; CHECK(StorageLog_Init()==HAL_OK && StorageLog_GetRecordCount()==16384);
  CHECK(StorageLog_Append(&status)==HAL_OK && StorageLog_GetRecordCount()==16257);
  CHECK(StorageLog_Init()==HAL_OK && next_sequence==16385 && StorageLog_GetRecordCount()==16257);
  return 0;
}
