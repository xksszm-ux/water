#pragma once
#include <stdint.h>
/* Register/API boundary only; no peripheral timing is simulated. */
typedef int HAL_StatusTypeDef;
typedef struct { int unused; } SPI_HandleTypeDef;
enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT };
enum { DISABLE=0, DMA_CIRCULAR=1, DMA_IT_HT=2, USART_CR3_DMAR=4,
  HAL_UART_STATE_READY=1, HAL_UART_STATE_BUSY_RX=2, HAL_UART_STATE_BUSY_TX=3,
  HAL_DMA_STATE_BUSY=1, HAL_UART_ERROR_NONE=0,
  HAL_UART_RXEVENT_IDLE=1, HAL_UART_RXEVENT_TC=2,
  USART1_IRQn=1, DMA1_Channel5_IRQn=2, USB_HP_CAN1_TX_IRQn=3,
  USB_LP_CAN1_RX0_IRQn=4, CAN1_SCE_IRQn=5,
  CAN_IT_TX_MAILBOX_EMPTY=1, CAN_IT_RX_FIFO0_MSG_PENDING=2,
  CAN_IT_RX_FIFO0_OVERRUN=4, CAN_IT_BUSOFF=8, CAN_IT_ERROR=16,
  HAL_CAN_ERROR_NONE=0, HAL_CAN_ERROR_TIMEOUT=1, HAL_CAN_ERROR_NOT_INITIALIZED=2,
  HAL_CAN_ERROR_NOT_READY=4, HAL_CAN_ERROR_NOT_STARTED=8, HAL_CAN_ERROR_PARAM=16,
  HAL_CAN_ERROR_INTERNAL=32, HAL_CAN_ERROR_BOF=64, HAL_CAN_ERROR_RX_FOV0=128,
  CAN_ESR_BOFF=1, CAN_FILTERMODE_IDLIST=1, CAN_FILTERSCALE_16BIT=1,
  CAN_FILTER_FIFO0=0, CAN_FILTER_ENABLE=1, CAN_MODE_LOOPBACK=1,
  HAL_CAN_STATE_READY=1, HAL_CAN_STATE_LISTENING=2, HAL_CAN_STATE_ERROR=3,
  CAN_TX_MAILBOX0=1, CAN_TX_MAILBOX1=2, CAN_TX_MAILBOX2=4,
  CAN_ID_STD=0, CAN_RTR_DATA=0, CAN_RX_FIFO0=0 };
typedef struct { volatile uint32_t CR3; } USART_TypeDef;
extern USART_TypeDef mock_usart;
#define USART1 (&mock_usart)
#define DMA1_Channel5 ((void *)5)
typedef struct { void *Instance; struct { uint32_t Mode; } Init; uint32_t State; } DMA_HandleTypeDef;
typedef struct { USART_TypeDef *Instance; DMA_HandleTypeDef *hdmarx;
  volatile uint32_t RxState, gState; } UART_HandleTypeDef;
typedef int HAL_UART_RxEventTypeTypeDef;
typedef struct { volatile uint32_t ESR; } CAN_TypeDef;
typedef struct { CAN_TypeDef *Instance; uint32_t State; struct { uint32_t Mode; } Init; } CAN_HandleTypeDef;
typedef struct { uint32_t FilterBank,FilterMode,FilterScale,FilterIdHigh,FilterIdLow,
  FilterMaskIdHigh,FilterMaskIdLow,FilterFIFOAssignment,FilterActivation,SlaveStartFilterBank; } CAN_FilterTypeDef;
typedef struct { uint32_t StdId,IDE,RTR,DLC,TransmitGlobalTime; } CAN_TxHeaderTypeDef;
typedef struct { uint32_t StdId,IDE,RTR,DLC; } CAN_RxHeaderTypeDef;
extern unsigned mock_critical_depth;
void MockEnterCritical(void);
void MockExitCritical(void);
#define __get_PRIMASK() (mock_critical_depth != 0U)
#define __disable_irq() MockEnterCritical()
#define __enable_irq() MockExitCritical()
#define __DMB() ((void)0)
#define __HAL_UART_CLEAR_OREFLAG(h) ((void)(h))
#define __HAL_DMA_DISABLE_IT(h,i) ((void)(h),(void)(i))
static inline void HAL_NVIC_DisableIRQ(int irq) { (void)irq; }
static inline void HAL_NVIC_EnableIRQ(int irq) { (void)irq; }
static inline void HAL_NVIC_ClearPendingIRQ(int irq) { (void)irq; }
HAL_StatusTypeDef HAL_UART_Abort(UART_HandleTypeDef *);
HAL_StatusTypeDef HAL_UART_DeInit(UART_HandleTypeDef *);
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *,uint8_t *,uint16_t);
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *,uint8_t *,uint16_t);
uint32_t HAL_UART_GetError(UART_HandleTypeDef *);
HAL_UART_RxEventTypeTypeDef HAL_UARTEx_GetRxEventType(UART_HandleTypeDef *);
HAL_StatusTypeDef HAL_CAN_ConfigFilter(CAN_HandleTypeDef *,const CAN_FilterTypeDef *);
HAL_StatusTypeDef HAL_CAN_Start(CAN_HandleTypeDef *);
HAL_StatusTypeDef HAL_CAN_Stop(CAN_HandleTypeDef *);
HAL_StatusTypeDef HAL_CAN_Init(CAN_HandleTypeDef *);
HAL_StatusTypeDef HAL_CAN_DeInit(CAN_HandleTypeDef *);
HAL_StatusTypeDef HAL_CAN_ActivateNotification(CAN_HandleTypeDef *,uint32_t);
HAL_StatusTypeDef HAL_CAN_DeactivateNotification(CAN_HandleTypeDef *,uint32_t);
HAL_StatusTypeDef HAL_CAN_AbortTxRequest(CAN_HandleTypeDef *,uint32_t);
HAL_StatusTypeDef HAL_CAN_AddTxMessage(CAN_HandleTypeDef *,const CAN_TxHeaderTypeDef *,uint8_t *,uint32_t *);
HAL_StatusTypeDef HAL_CAN_GetRxMessage(CAN_HandleTypeDef *,uint32_t,CAN_RxHeaderTypeDef *,uint8_t *);
HAL_StatusTypeDef HAL_CAN_ResetError(CAN_HandleTypeDef *);
uint32_t HAL_CAN_GetTxMailboxesFreeLevel(CAN_HandleTypeDef *);
uint32_t HAL_CAN_IsTxMessagePending(CAN_HandleTypeDef *,uint32_t);
uint32_t HAL_CAN_GetRxFifoFillLevel(const CAN_HandleTypeDef *,uint32_t);
uint32_t HAL_CAN_GetError(CAN_HandleTypeDef *);
