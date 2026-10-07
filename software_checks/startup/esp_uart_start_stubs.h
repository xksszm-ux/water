#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
enum { ESP_OK=0, ESP_FAIL=-1, ESP_ERR_INVALID_STATE=-2, ESP_ERR_NO_MEM=-3 };
typedef int gpio_num_t;
typedef int BaseType_t;
typedef uint32_t TickType_t;
typedef void *QueueHandle_t;
typedef void *TaskHandle_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
#define portTICK_PERIOD_MS 10U
#define pdMS_TO_TICKS(ms) ((ms)/10U)
#define portMAX_DELAY UINT32_MAX
#define pdTRUE 1
#define pdPASS 1
#define GPIO_PULLUP_ONLY 1
#define UART_NUM_2 2
#define UART_DATA_8_BITS 8
#define UART_PARITY_DISABLE 0
#define UART_STOP_BITS_1 1
#define UART_HW_FLOWCTRL_DISABLE 0
#define UART_SCLK_DEFAULT 0
#define UART_PIN_NO_CHANGE -1
#define eSetBits 1
#define ESP_LOGI(...) ((void)TAG)
#define ESP_LOGW(...) ((void)TAG)
enum { UART_DATA, UART_FIFO_OVF, UART_BUFFER_FULL, UART_PARITY_ERR, UART_FRAME_ERR, UART_BREAK };
typedef struct { int type; size_t size; } uart_event_t;
typedef struct { int baud_rate,data_bits,parity,stop_bits,flow_ctrl,rx_flow_ctrl_thresh,source_clk;
  struct { int allow_pd; } flags; } uart_config_t;
TickType_t xTaskGetTickCount(void);
BaseType_t xPortGetCoreID(void);
QueueHandle_t xQueueCreate(unsigned,size_t);
BaseType_t xQueueSend(QueueHandle_t,const void *,TickType_t);
BaseType_t xQueueReceive(QueueHandle_t,void *,TickType_t);
BaseType_t xQueuePeek(QueueHandle_t,void *,TickType_t);
BaseType_t xQueueOverwrite(QueueHandle_t,const void *);
BaseType_t xQueueReset(QueueHandle_t);
void vQueueDelete(QueueHandle_t);
void vTaskDelete(TaskHandle_t);
BaseType_t xTaskCreatePinnedToCore(void (*)(void *),const char *,unsigned,void *,unsigned,TaskHandle_t *,BaseType_t);
void xTaskNotifyGive(TaskHandle_t);
uint32_t ulTaskNotifyTake(BaseType_t,TickType_t);
BaseType_t xTaskNotify(TaskHandle_t,uint32_t,int);
BaseType_t xTaskNotifyWait(uint32_t,uint32_t,uint32_t *,TickType_t);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
esp_err_t uart_driver_install(int,int,int,int,QueueHandle_t *,int);
esp_err_t uart_driver_delete(int);
esp_err_t uart_param_config(int,const uart_config_t *);
esp_err_t uart_set_pin(int,int,int,int,int);
esp_err_t uart_flush_input(int);
esp_err_t gpio_set_pull_mode(gpio_num_t,int);
int uart_read_bytes(int,void *,size_t,TickType_t);
int uart_write_bytes(int,const char *,size_t);
esp_err_t uart_wait_tx_done(int,TickType_t);
