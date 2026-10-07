/* Execute production driver; replace only HAL registers, IRQ masks and events. */
#include "../../stm32/Drivers/UART/uart_driver.c"
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
USART_TypeDef mock_usart;
static DMA_HandleTypeDef dma;
UART_HandleTypeDef huart1;
unsigned mock_critical_depth;
static unsigned unlocked_probes;
static uint32_t hal_error, events;
static int rx_event;
static bool abort_fail, receive_fail;
void MockEnterCritical(void) { ++mock_critical_depth; }
void MockExitCritical(void) { --mock_critical_depth; }
uint32_t osThreadFlagsSet(osThreadId_t t,uint32_t f) { (void)t; events|=f; return events; }
uint32_t osKernelGetTickCount(void) { return 0; }
void MX_USART1_UART_Init(void) {
  dma.Instance=DMA1_Channel5; dma.Init.Mode=DMA_CIRCULAR;
  huart1.Instance=USART1; huart1.hdmarx=&dma;
  huart1.gState=HAL_UART_STATE_READY;
}
HAL_StatusTypeDef HAL_UART_Abort(UART_HandleTypeDef *h) {
  h->gState=HAL_UART_STATE_READY; return abort_fail ? HAL_ERROR : HAL_OK;
}
HAL_StatusTypeDef HAL_UART_DeInit(UART_HandleTypeDef *h) { (void)h; return HAL_OK; }
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *h,uint8_t *b,uint16_t n) {
  (void)b;(void)n; h->RxState=HAL_UART_STATE_BUSY_RX;
  h->hdmarx->State=HAL_DMA_STATE_BUSY; h->Instance->CR3=USART_CR3_DMAR;
  return receive_fail ? HAL_ERROR : HAL_OK;
}
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *h,uint8_t *b,uint16_t n) {
  (void)b;(void)n; h->gState=HAL_UART_STATE_BUSY_TX; return HAL_OK;
}
uint32_t HAL_UART_GetError(UART_HandleTypeDef *h) {
  (void)h; if(mock_critical_depth==0) ++unlocked_probes; return hal_error;
}
HAL_UART_RxEventTypeTypeDef HAL_UARTEx_GetRxEventType(UART_HandleTypeDef *h) { (void)h; return rx_event; }
int UartDriverChecks_Run(void) {
  uint8_t byte, data[2]={1,2};
  MX_USART1_UART_Init();
  CHECK(UartDriver_Init((void *)1)==HAL_OK);
  unlocked_probes=0;
  CHECK(UartDriver_Service()==HAL_OK && unlocked_probes==0);
  CHECK(UartDriver_Send(data,2)==HAL_OK && UartDriver_Service()==HAL_OK);
  CHECK(UartDriver_Send(data,2)==HAL_BUSY);
  huart1.gState=HAL_UART_STATE_READY; HAL_UART_TxCpltCallback(&huart1);
  CHECK(UartDriver_Service()==HAL_OK && !UartDriver_IsTxPending());
  CHECK(driver_diagnostics.transmitted_bytes==2);
  rx_event=HAL_UART_RXEVENT_IDLE;
  for(unsigned i=0;i<64;++i) dma_rx_buffer[i]=(uint8_t)i;
  HAL_UARTEx_RxEventCallback(&huart1,3);
  HAL_UARTEx_RxEventCallback(&huart1,3); /* repeated IDLE copies nothing */
  CHECK(driver_diagnostics.received_bytes==3);
  for(unsigned i=0;i<3;++i) CHECK(UartDriver_ReadByte(&byte) && byte==i);
  CHECK(!UartDriver_ReadByte(&byte));
  rx_event=HAL_UART_RXEVENT_TC; HAL_UARTEx_RxEventCallback(&huart1,64);
  rx_event=HAL_UART_RXEVENT_IDLE; HAL_UARTEx_RxEventCallback(&huart1,2);
  for(unsigned i=3;i<64;++i) CHECK(UartDriver_ReadByte(&byte) && byte==i);
  CHECK(UartDriver_ReadByte(&byte) && byte==0);
  CHECK(UartDriver_ReadByte(&byte) && byte==1 && !UartDriver_ReadByte(&byte));
  rx_event=HAL_UART_RXEVENT_TC;
  for(unsigned i=0;i<5;++i) HAL_UARTEx_RxEventCallback(&huart1,64);
  CHECK(driver_diagnostics.dropped_bytes>0 && (UartDriver_ConsumeEvents()&UART_DRIVER_EVENT_OVERFLOW));
  hal_error=1; CHECK(UartDriver_Service()==HAL_ERROR && !UartDriver_IsReady());
  CHECK(UartDriver_Send(data,2)==HAL_ERROR);
  hal_error=0; CHECK(UartDriver_Recover((void *)1)==HAL_OK && !UartDriver_ReadByte(&byte));
  huart1.gState=HAL_UART_STATE_BUSY_TX;
  CHECK(UartDriver_Service()==HAL_ERROR); /* genuine HAL/pending disagreement */
  abort_fail=true; CHECK(UartDriver_Recover((void *)1)==HAL_ERROR);
  abort_fail=false; receive_fail=true; CHECK(UartDriver_Recover((void *)1)==HAL_ERROR);
  receive_fail=false; CHECK(UartDriver_Recover((void *)1)==HAL_OK);
  CHECK(mock_critical_depth==0);
  return 0;
}
