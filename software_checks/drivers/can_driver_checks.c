#include "../../stm32/Drivers/CAN/can_driver.c"
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static uint32_t hal_error, mailbox_pending;
static unsigned reads, fifo_count;
static bool flood, read_fail, init_fail;
static uint8_t frames[8][8];
HAL_StatusTypeDef HAL_CAN_ConfigFilter(CAN_HandleTypeDef *h,const CAN_FilterTypeDef *f) { (void)h;(void)f; return HAL_OK; }
HAL_StatusTypeDef HAL_CAN_Start(CAN_HandleTypeDef *h) { h->State=HAL_CAN_STATE_LISTENING; return HAL_OK; }
HAL_StatusTypeDef HAL_CAN_Stop(CAN_HandleTypeDef *h) { h->State=HAL_CAN_STATE_READY; return HAL_OK; }
HAL_StatusTypeDef HAL_CAN_Init(CAN_HandleTypeDef *h) { h->State=HAL_CAN_STATE_READY; return init_fail ? HAL_ERROR : HAL_OK; }
HAL_StatusTypeDef HAL_CAN_DeInit(CAN_HandleTypeDef *h) { (void)h; return HAL_OK; }
HAL_StatusTypeDef HAL_CAN_ActivateNotification(CAN_HandleTypeDef *h,uint32_t n) { (void)h;(void)n; return HAL_OK; }
HAL_StatusTypeDef HAL_CAN_DeactivateNotification(CAN_HandleTypeDef *h,uint32_t n) { (void)h;(void)n; return HAL_OK; }
HAL_StatusTypeDef HAL_CAN_AbortTxRequest(CAN_HandleTypeDef *h,uint32_t n) { (void)h;(void)n; mailbox_pending=0; return HAL_OK; }
HAL_StatusTypeDef HAL_CAN_AddTxMessage(CAN_HandleTypeDef *h,const CAN_TxHeaderTypeDef *t,uint8_t *b,uint32_t *m) {
  (void)h;(void)t;(void)b; *m=CAN_TX_MAILBOX0; mailbox_pending=1; return HAL_OK;
}
HAL_StatusTypeDef HAL_CAN_GetRxMessage(CAN_HandleTypeDef *h,uint32_t f,CAN_RxHeaderTypeDef *r,uint8_t *b) {
  (void)h;(void)f; if(read_fail) return HAL_ERROR;
  *r=(CAN_RxHeaderTypeDef){.StdId=CAN_ID_MOTOR_COMMAND,.DLC=8};
  memcpy(b,frames[reads%8],8); ++reads; if(!flood) --fifo_count; return HAL_OK;
}
HAL_StatusTypeDef HAL_CAN_ResetError(CAN_HandleTypeDef *h) { (void)h;hal_error=0; return HAL_OK; }
uint32_t HAL_CAN_GetTxMailboxesFreeLevel(CAN_HandleTypeDef *h) { (void)h; return 3; }
uint32_t HAL_CAN_IsTxMessagePending(CAN_HandleTypeDef *h,uint32_t n) { (void)h;(void)n; return mailbox_pending; }
uint32_t HAL_CAN_GetRxFifoFillLevel(const CAN_HandleTypeDef *h,uint32_t f) {
  (void)h;(void)f; /* finite mock ceiling makes an unbounded old ISR fail, not hang */
  return flood ? (reads<8 ? 3 : 0) : fifo_count;
}
uint32_t HAL_CAN_GetError(CAN_HandleTypeDef *h) { (void)h;return hal_error; }
int CanDriverChecks_Run(void) {
  CAN_TypeDef registers={0}; CAN_HandleTypeDef h={.Instance=&registers,.State=HAL_CAN_STATE_READY};
  CanDriverFrame_t frame;
  for(unsigned i=0;i<8;++i) {
    CanMotorCommand_t c={.left_output_permille=100,.enable=true,.sequence=(uint8_t)i};
    if(i==1) c.enable=false;
    CanProtocol_EncodeControl(&c,frames[i]);
  }
  CHECK(CanDriver_Init(&h,(void *)1)==HAL_OK);
  flood=true; reads=0; HAL_CAN_RxFifo0MsgPendingCallback(&h);
  CHECK(reads==3 && CanDriver_Receive(&frame) && frame.data[6]==1); /* STOP first */
  CHECK(CanDriver_Receive(&frame) && frame.data[6]==2 && !CanDriver_Receive(&frame));
  HAL_CAN_RxFifo0MsgPendingCallback(&h); CHECK(reads==6);
  CHECK(CanDriver_Receive(&frame) && frame.data[6]==5);
  flood=false; fifo_count=1; read_fail=true;
  HAL_CAN_RxFifo0MsgPendingCallback(&h);
  CHECK(CanDriver_Service(0,50)==HAL_ERROR);
  read_fail=false; CHECK(CanDriver_Recover()==HAL_OK && !CanDriver_Receive(&frame));
  CHECK(CanDriver_Send(CAN_ID_ROBOT_STATUS,frames[0],8)==HAL_OK);
  CHECK(CanDriver_Service(50,50)==HAL_OK);
  CHECK(CanDriver_Service(51,50)==HAL_TIMEOUT && !CanDriver_IsTransmitPending());
  CHECK(CanDriver_Send(CAN_ID_ROBOT_STATUS,frames[0],8)==HAL_OK);
  HAL_CAN_TxMailbox0CompleteCallback(&h); CHECK(!CanDriver_IsTransmitPending());
  hal_error=HAL_CAN_ERROR_BOF; CHECK(CanDriver_Service(0,50)==HAL_ERROR);
  hal_error=0; init_fail=true; CHECK(CanDriver_Recover()==HAL_ERROR && !CanDriver_IsReady());
  init_fail=false; CHECK(CanDriver_Recover()==HAL_OK);
  CHECK(mock_critical_depth==0);
  return 0;
}
