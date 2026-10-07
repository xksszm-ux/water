/* No scheduler: task creation captures handles but never invokes task bodies.
   Production start/cleanup is executed; UART/NimBLE runtime is not modeled. */
#include "esp_uart_start_stubs.h"
#include "../../esp32/main/uart/robot_uart_link.c"
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static unsigned point, fail_at, queues, tasks, notifications;
static bool installed, bad_release;
static bool Fail(void) { return ++point==fail_at; }
TickType_t xTaskGetTickCount(void) { return 0; }
BaseType_t xPortGetCoreID(void) { return 0; }
QueueHandle_t xQueueCreate(unsigned n,size_t s) { (void)n;(void)s; if(Fail()) return NULL; ++queues; return (void *)(uintptr_t)point; }
void vQueueDelete(QueueHandle_t q) { (void)q; --queues; }
BaseType_t xQueueSend(QueueHandle_t q,const void *d,TickType_t t) { (void)q;(void)d;(void)t; return pdPASS; }
BaseType_t xQueueReceive(QueueHandle_t q,void *d,TickType_t t) { (void)q;(void)d;(void)t; return 0; }
BaseType_t xQueuePeek(QueueHandle_t q,void *d,TickType_t t) { return xQueueReceive(q,d,t); }
BaseType_t xQueueOverwrite(QueueHandle_t q,const void *d) { (void)q;(void)d; return pdPASS; }
BaseType_t xQueueReset(QueueHandle_t q) { (void)q; return pdPASS; }
void vTaskDelete(TaskHandle_t t) { (void)t; --tasks; }
BaseType_t xTaskCreatePinnedToCore(void (*f)(void *),const char *n,unsigned s,void *a,unsigned p,TaskHandle_t *h,BaseType_t core) {
  (void)f;(void)n;(void)s;(void)a;(void)p;(void)core;
  if(Fail()) return 0; *h=(void *)(uintptr_t)point; ++tasks; return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t h) { (void)h; ++notifications; if(tasks!=2 || point!=9) bad_release=true; }
uint32_t ulTaskNotifyTake(BaseType_t a,TickType_t t) { (void)a;(void)t; return 0; }
BaseType_t xTaskNotify(TaskHandle_t h,uint32_t v,int a) { (void)h;(void)v;(void)a; return pdPASS; }
BaseType_t xTaskNotifyWait(uint32_t a,uint32_t b,uint32_t *v,TickType_t t) { (void)a;(void)b;(void)t; *v=0; return 0; }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return NULL; }
esp_err_t uart_driver_install(int p,int r,int t,int n,QueueHandle_t *q,int f) {
  (void)p;(void)r;(void)t;(void)n;(void)f;
  if(Fail()) return ESP_FAIL; installed=true; *q=(void *)99; return ESP_OK;
}
esp_err_t uart_driver_delete(int p) { (void)p; installed=false; return ESP_OK; }
esp_err_t uart_param_config(int p,const uart_config_t *c) { (void)p;(void)c; return Fail() ? ESP_FAIL : ESP_OK; }
esp_err_t uart_set_pin(int p,int tx,int rx,int rts,int cts) { (void)p;(void)tx;(void)rx;(void)rts;(void)cts; return Fail() ? ESP_FAIL : ESP_OK; }
esp_err_t uart_flush_input(int p) { (void)p; return Fail() ? ESP_FAIL : ESP_OK; }
esp_err_t gpio_set_pull_mode(gpio_num_t p,int m) { (void)p;(void)m; return Fail() ? ESP_FAIL : ESP_OK; }
int uart_read_bytes(int p,void *b,size_t n,TickType_t t) { (void)p;(void)b;(void)n;(void)t; return 0; }
int uart_write_bytes(int p,const char *b,size_t n) { (void)p;(void)b; return (int)n; }
esp_err_t uart_wait_tx_done(int p,TickType_t t) { (void)p;(void)t; return ESP_OK; }
int EspUartStartChecks_Run(void) {
  for(unsigned failure=1;failure<=9;++failure) {
    point=0; fail_at=failure; notifications=0;
    CHECK(RobotUartLink_Start()!=ESP_OK);
    CHECK(!installed && queues==0 && tasks==0 && notifications==0);
    CHECK(!driver_started && !link_online && !ack_queue && !status_mailbox && !uart_event_queue);
    CHECK(!rx_task_handle && !link_task_handle && RobotUartLink_RequestStop()==ESP_ERR_INVALID_STATE);
    point=0; fail_at=0;
    CHECK(RobotUartLink_Start()==ESP_OK && point==9);
    CHECK(installed && queues==2 && tasks==2 && notifications==2 && !bad_release);
    CHECK(!RobotUartLink_IsOnline()); /* creation is not a completed STOP handshake */
    CHECK(RobotUartLink_Start()==ESP_ERR_INVALID_STATE && point==9);
    /* Only the test calls cleanup after simulated success; no public Stop API. */
    DeleteResources(); CHECK(!installed && queues==0 && tasks==0);
  }
  return 0;
}
