#include "esp_ble_stubs.h"
#include <string.h>
#include "../../esp32/main/ble/robot_ble.c"
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
/* Run production callbacks/tasks sequentially at OS waits. This exercises
 * cleanup decisions, not concurrent scheduling, RF, or actual cryptography. */
enum { F_NONE,F_NVS,F_INIT,F_GPIO,F_COUNT,F_ADD,F_NAME,F_CALLOUT,F_HOST,
 F_START_TIMEOUT,F_PRIVACY,F_CALLOUT_RESET,F_ADV_FIELDS,F_ADV_RSP,F_ADV_START,F_STATUS };
struct mock_ble_hs_cfg ble_hs_cfg;
static EventBits_t event_bits;
static struct ble_npl_eventq eventq;
static struct ble_npl_event *queued_event;
static struct os_mbuf notification_buffer;
static TaskHandle_t current_task;
static uint32_t now_ticks;
static int failure, deinit_error, stop_error, terminate_error, store_error, unpair_error, security_error, flat_error, append_error, notify_error, stop_request_error;
static bool adv_active, have_bond, secured, bonded, conn_find_error, stop_create_error, stop_wait_timeout, status_wait_timeout, missing_host_exit, mbuf_error, fresh_status, fresh_can;
static unsigned init_calls, deinit_calls, stop_requests, terminate_calls, unpair_calls, notifications, event_deinits, callout_deinits;
void MockBleLog(const char *tag,const char *format,...) {(void)tag;(void)format;}
TickType_t xTaskGetTickCount(void) {return now_ticks;}
TaskHandle_t xTaskGetCurrentTaskHandle(void) {return current_task;}
EventGroupHandle_t xEventGroupCreateStatic(StaticEventGroup_t *s) {(void)s;return &event_bits;}
EventBits_t xEventGroupClearBits(EventGroupHandle_t e,EventBits_t b) {*e &= ~b;return *e;}
EventBits_t xEventGroupSetBits(EventGroupHandle_t e,EventBits_t b) {*e |= b;return *e;}
EventBits_t xEventGroupWaitBits(EventGroupHandle_t e,EventBits_t b,BaseType_t clear,BaseType_t all,TickType_t t) {
  (void)clear;(void)all;(void)t;
  if(b & BLE_EVENT_START_OK) {
    if(failure==F_START_TIMEOUT) return 0;
    OnSync();
    for(unsigned n=0;n<6 && !(*e&(BLE_EVENT_START_OK|BLE_EVENT_START_FAIL));++n) AdvertisingRetryEvent(NULL);
  } else if(b==BLE_EVENT_STOP_DONE) {
    if(stop_wait_timeout) return 0;
    TaskHandle_t saved=current_task;current_task=(void *)3;StopTask(NULL);current_task=saved;
  } else if(b==BLE_EVENT_STATUS_EXIT) {
    if(status_wait_timeout) return 0;
    TaskHandle_t saved=current_task;current_task=(void *)2;StatusTask(NULL);current_task=saved;
  } else if(b==BLE_EVENT_HOST_EXIT && missing_host_exit) return 0;
  return *e;
}
BaseType_t xTaskCreatePinnedToCore(void (*f)(void *),const char *name,unsigned stack,void *a,unsigned p,TaskHandle_t *h,BaseType_t core) {
  (void)name;(void)stack;(void)a;(void)p;(void)core;
  if((f==HostTask && failure==F_HOST) || (f==StatusTask && failure==F_STATUS)) return 0;
  *h=f==HostTask ? (void *)1:(void *)2;return pdPASS;
}
BaseType_t xTaskCreate(void (*f)(void *),const char *n,unsigned s,void *a,unsigned p,TaskHandle_t *h) {
  (void)f;(void)n;(void)s;(void)a;(void)p;
  if(stop_create_error) return 0;*h=(void *)3;return pdPASS;
}
void vTaskDelete(TaskHandle_t t) {(void)t;}
void xTaskNotifyGive(TaskHandle_t t) {(void)t;}
uint32_t ulTaskNotifyTake(BaseType_t c,TickType_t t) {(void)c;now_ticks+=t;status_stop_requested=true;return 0;}
esp_err_t gpio_config(const gpio_config_t *c) {(void)c;return failure==F_GPIO ? ESP_FAIL:ESP_OK;}
int gpio_get_level(int pin) {(void)pin;return 1;}
esp_err_t nvs_flash_init(void) {return failure==F_NVS ? ESP_FAIL:ESP_OK;}
esp_err_t nimble_port_init(void) {++init_calls;return failure==F_INIT ? ESP_FAIL:ESP_OK;}
esp_err_t nimble_port_deinit(void) {++deinit_calls;return deinit_error;}
int nimble_port_stop(void) {
  if(stop_error) return stop_error;
  TaskHandle_t saved=current_task;current_task=(void *)1;HostTask(NULL);current_task=saved;return 0;
}
void nimble_port_run(void) {}
struct ble_npl_eventq *nimble_port_get_dflt_eventq(void) {return &eventq;}
void ble_npl_event_init(struct ble_npl_event *e,void (*f)(struct ble_npl_event *),void *a) {(void)a;e->cb=f;}
void ble_npl_event_deinit(struct ble_npl_event *e) {e->cb=NULL;++event_deinits;}
void ble_npl_eventq_put(struct ble_npl_eventq *q,struct ble_npl_event *e) {(void)q;queued_event=e;}
int ble_npl_callout_init(struct ble_npl_callout *c,struct ble_npl_eventq *q,void (*f)(struct ble_npl_event *),void *a) {
  (void)q;(void)a;c->event.cb=f;return failure==F_CALLOUT ? 1:0;
}
void ble_npl_callout_deinit(struct ble_npl_callout *c) {c->event.cb=NULL;++callout_deinits;}
void ble_npl_callout_stop(struct ble_npl_callout *c) {(void)c;}
int ble_npl_callout_reset(struct ble_npl_callout *c,uint32_t n) {(void)c;(void)n;return failure==F_CALLOUT_RESET ? 1:0;}
uint32_t ble_npl_time_ms_to_ticks32(uint32_t n) {return n/10;}
int ble_addr_cmp(const ble_addr_t *a,const ble_addr_t *b) {return memcmp(a,b,sizeof(*a));}
int ble_store_util_bonded_peers(ble_addr_t *a,int *n,int max) {(void)max;memset(a,0,sizeof(*a));*n=have_bond ? 1:0;return store_error;}
int ble_gap_adv_stop(void) {if(!adv_active)return BLE_HS_EALREADY;adv_active=false;return 0;}
int ble_gap_unpair(const ble_addr_t *a) {(void)a;++unpair_calls;if(!unpair_error)have_bond=false;return unpair_error;}
int ble_hs_pvcy_rpa_config(int n) {(void)n;return failure==F_PRIVACY ? 1:0;}
int ble_gap_terminate(uint16_t h,int r) {(void)h;(void)r;++terminate_calls;return terminate_error;}
int ble_gap_conn_find(uint16_t h,struct ble_gap_conn_desc *d) {
  if(conn_find_error)return BLE_HS_ENOTCONN;memset(d,0,sizeof(*d));d->conn_handle=h;
  d->sec_state.encrypted=secured;d->sec_state.bonded=bonded;return 0;
}
int ble_gap_security_initiate(uint16_t h) {(void)h;return security_error;}
int ble_gap_adv_active(void) {return adv_active;}
int ble_gap_adv_set_fields(const struct ble_hs_adv_fields *f) {(void)f;return failure==F_ADV_FIELDS ? 1:0;}
int ble_gap_adv_rsp_set_fields(const struct ble_hs_adv_fields *f) {(void)f;return failure==F_ADV_RSP ? 1:0;}
int ble_gap_adv_start(uint8_t a,const ble_addr_t *b,int t,const struct ble_gap_adv_params *p,int (*f)(struct ble_gap_event *,void *),void *arg) {
  (void)a;(void)b;(void)t;(void)p;(void)f;(void)arg;if(failure==F_ADV_START)return 1;adv_active=true;return 0;
}
int ble_hs_mbuf_to_flat(const struct os_mbuf *m,void *p,uint16_t n,uint16_t *len) {
  if(flat_error)return flat_error;*len=m->length;if(*len>n)return 1;memcpy(p,m->data,*len);return 0;
}
struct os_mbuf *ble_hs_mbuf_from_flat(const void *p,uint16_t n) {
  if(mbuf_error)return NULL;notification_buffer.length=n;memcpy(notification_buffer.data,p,n);return &notification_buffer;
}
int os_mbuf_append(struct os_mbuf *m,const void *p,uint16_t n) {
  if(append_error || m->length+n>sizeof(m->data))return 1;memcpy(m->data+m->length,p,n);m->length+=n;return 0;
}
int ble_gatts_notify_custom(uint16_t h,uint16_t a,struct os_mbuf *m) {(void)h;(void)a;(void)m;++notifications;return notify_error;}
int ble_gatts_count_cfg(const struct ble_gatt_svc_def *s) {(void)s;return failure==F_COUNT ? 1:0;}
int ble_gatts_add_svcs(const struct ble_gatt_svc_def *s) {(void)s;status_value_handle=42;return failure==F_ADD ? 1:0;}
void ble_svc_gap_init(void) {}
void ble_svc_gatt_init(void) {}
int ble_svc_gap_device_name_set(const char *n) {(void)n;return failure==F_NAME ? 1:0;}
void ble_store_config_init(void) {}
esp_err_t RobotUartLink_RequestStop(void) {++stop_requests;return stop_request_error;}
bool RobotUartLink_IsOnline(void) {return fresh_status;}
bool RobotUartLink_GetStatus(RobotProtocolStatus_t *s,uint32_t *a) {
  if(!fresh_status)return false;memset(s,0,sizeof(*s));s->battery_mv=4500;s->owner=ROBOT_PROTOCOL_OWNER_NONE;
  s->distance_mm=ROBOT_PROTOCOL_INVALID_U16;*a=100;return true;
}
bool RobotUartLink_GetCanFeedback(RobotCanFeedback_t *s) {
  if(!fresh_can)return false;
  *s=(RobotCanFeedback_t){.flags=7,.sequence=9,.result=6,.rejected=3,.replays=1,.age_ms=100};return true;
}
static void Reset(void) {
  /* A new case starts at reset-state; only explicit restart cases reuse it. */
  memset(&ble_diagnostics,0,sizeof(ble_diagnostics));event_bits=0;current_task=(void *)99;now_ticks=0;
  lifecycle_events=NULL;host_task_handle=status_task_handle=stop_task_handle=NULL;
  ble_started=ble_starting=ble_faulted=nimble_initialized=host_ready=host_synced=host_stopping=false;
  status_stop_requested=lifecycle_operation_active=lifecycle_cleanup_failed=stop_wait_abandoned=false;
  connection_handle=BLE_HS_CONN_HANDLE_NONE;notify_enabled=connection_encrypted=connection_pairing_authorized=false;
  pairing_window_open=pairing_reset_requested=pairing_reset_pending=pairing_reset_retry_active=delete_bond_after_disconnect=false;
  pairing_reset_event_initialized=advertising_retry_callout_initialized=advertising_retry_pending=false;
  security_timeout_active=false;advertising_failure_count=0;RobotBleDisconnectWatch_Clear(&disconnect_watch);
  failure=deinit_error=stop_error=terminate_error=store_error=unpair_error=security_error=flat_error=append_error=notify_error=stop_request_error=0;
  adv_active=have_bond=secured=bonded=conn_find_error=stop_create_error=stop_wait_timeout=status_wait_timeout=missing_host_exit=mbuf_error=fresh_status=fresh_can=false;
  init_calls=deinit_calls=stop_requests=terminate_calls=unpair_calls=notifications=event_deinits=callout_deinits=0;queued_event=NULL;
}
static void Connect(bool known,bool pairing) {
  have_bond=known;pairing_window_open=pairing;pairing_window_deadline_ms=NowMs()+60000;
  struct ble_gap_event e={.type=BLE_GAP_EVENT_CONNECT,.connect={.status=0,.conn_handle=7}};GapEvent(&e,NULL);
}
static void Encrypt(bool on) {
  secured=bonded=on;
  struct ble_gap_event e={.type=BLE_GAP_EVENT_ENC_CHANGE,.enc_change={.status=0,.conn_handle=7}};GapEvent(&e,NULL);
}
int EspBleChecks_Run(void) {
  for(int f=F_NVS;f<=F_STATUS;++f) {
    Reset();failure=f;CHECK(RobotBle_Start()!=ESP_OK && !RobotBle_IsReady());
    CHECK(!lifecycle_operation_active && !ble_starting && !nimble_initialized && !host_task_handle && !status_task_handle);
    CHECK(!pairing_reset_event_initialized && !advertising_retry_callout_initialized);
    failure=0;CHECK(RobotBle_Start()==ESP_OK && RobotBle_IsReady());CHECK(RobotBle_Stop()==ESP_OK && !nimble_initialized);
  }
  Reset();CHECK(RobotBle_Start()==ESP_OK);CHECK(RobotBle_Start()==ESP_ERR_INVALID_STATE);
  CHECK((services[0].characteristics[0].flags & BLE_GATT_CHR_F_WRITE_ENC)!=0);
  CHECK((services[0].characteristics[1].flags & BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC)!=0);
  CHECK((services[0].characteristics[2].flags & BLE_GATT_CHR_F_READ_ENC)!=0);
  current_task=host_task_handle;CHECK(RobotBle_Stop()==ESP_ERR_INVALID_STATE);current_task=(void *)99;
  lifecycle_operation_active=true;CHECK(RobotBle_Stop()==ESP_ERR_INVALID_STATE);lifecycle_operation_active=false;
  CHECK(RobotBle_Stop()==ESP_OK && deinit_calls==1 && callout_deinits==1 && event_deinits==1);
  CHECK(RobotBle_Start()==ESP_OK && RobotBle_Stop()==ESP_OK);
  Reset();failure=F_GPIO;deinit_error=ESP_FAIL;CHECK(RobotBle_Start()!=ESP_OK && ble_faulted && nimble_initialized);
  failure=0;CHECK(RobotBle_Start()==ESP_ERR_INVALID_STATE);
  for(unsigned kind=0;kind<5;++kind) {
    Reset();CHECK(RobotBle_Start()==ESP_OK);
    stop_create_error=kind==0;stop_error=kind==1 ? 1:0;missing_host_exit=kind==2;
    status_wait_timeout=kind==3;deinit_error=kind==4 ? ESP_FAIL:ESP_OK;
    CHECK(RobotBle_Stop()!=ESP_OK && ble_faulted && nimble_initialized && !lifecycle_operation_active);
    CHECK(RobotBle_Start()==ESP_ERR_INVALID_STATE);
    if(kind<4)CHECK(deinit_calls==0);
    if(kind==3)CHECK(lifecycle_cleanup_failed && status_task_handle && pairing_reset_event_initialized);
  }
  Reset();CHECK(RobotBle_Start()==ESP_OK);stop_wait_timeout=true;
  CHECK(RobotBle_Stop()==ESP_ERR_TIMEOUT && stop_wait_abandoned && stop_task_handle);
  stop_wait_timeout=false;StopTask(NULL);CHECK(!nimble_initialized && ble_faulted && RobotBle_Start()==ESP_ERR_INVALID_STATE);
  Reset();CHECK(RobotBle_Start()==ESP_OK);Connect(false,false);
  CHECK(!connection_pairing_authorized && terminate_calls==1 && stop_requests>0);
  Reset();CHECK(RobotBle_Start()==ESP_OK);Connect(false,true);CHECK(connection_pairing_authorized && security_timeout_active);
  ServiceSecurityTimeout(29999);CHECK(terminate_calls==0);terminate_error=1;ServiceSecurityTimeout(30000);
  CHECK(terminate_calls==1 && !connection_pairing_authorized);ServiceSecurityTimeout(30200);CHECK(terminate_calls==2);
  terminate_error=BLE_HS_ENOTCONN;ServiceSecurityTimeout(30400);CHECK(!RobotBle_IsConnected());
  Reset();CHECK(RobotBle_Start()==ESP_OK);Connect(true,false);Encrypt(true);
  CHECK(connection_encrypted && !security_timeout_active);
  struct os_mbuf m={.length=10,.data={1,0,0x34,0x12,0,0,0,0,0x19,0x1f}};
  struct ble_gatt_access_ctxt c={.op=BLE_GATT_ACCESS_OP_WRITE_CHR,.om=&m};
  CHECK(CommandAccess(8,0,&c,NULL)==BLE_ATT_ERR_WRITE_NOT_PERMITTED);
  CHECK(CommandAccess(7,0,&c,NULL)==0 && ble_diagnostics.stop_commands==1);
  stop_request_error=ESP_ERR_INVALID_STATE;CHECK(CommandAccess(7,0,&c,NULL)==BLE_ATT_ERR_UNLIKELY && ble_diagnostics.stop_commands==1);stop_request_error=0;
  m.length=9;CHECK(CommandAccess(7,0,&c,NULL)==BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN);m.length=10;
  flat_error=1;CHECK(CommandAccess(7,0,&c,NULL)==BLE_ATT_ERR_UNLIKELY);flat_error=0;
  m.data[9]^=1;CHECK(CommandAccess(7,0,&c,NULL)==BLE_ATT_ERR_VALUE_NOT_ALLOWED);m.data[9]^=1;
  m.data[1]=ROBOT_BLE_OPCODE_DRIVE;uint16_t crc=RobotProtocol_Crc16CcittFalse(m.data,8);m.data[8]=(uint8_t)crc;m.data[9]=(uint8_t)(crc>>8);
  unsigned stops_before=stop_requests;CHECK(CommandAccess(7,0,&c,NULL)==BLE_ATT_ERR_WRITE_NOT_PERMITTED && stop_requests==stops_before+1);
  c.op=BLE_GATT_ACCESS_OP_READ_CHR;m.length=0;CHECK(StatusAccess(7,0,&c,NULL)==0 && m.length==20);
  CHECK(!(m.data[4]&ROBOT_BLE_LINK_STATUS_FRESH) && m.data[6]==0xff && m.data[7]==0xff);
  fresh_status=true;m.length=0;status_sequence=UINT16_MAX;
  CHECK(StatusAccess(7,0,&c,NULL)==0 && (m.data[4]&ROBOT_BLE_LINK_STATUS_FRESH) && m.data[6]==0x94 && m.data[7]==0x11 && status_sequence==0);
  m.length=0;CHECK(CanFeedbackAccess(7,0,&c,NULL)==0 && m.length==ROBOT_BLE_CAN_FEEDBACK_SIZE);
  fresh_can=true;m.length=0;CHECK(CanFeedbackAccess(7,0,&c,NULL)==0 && m.data[1]==7 && m.data[2]==9 && m.data[3]==6);
  append_error=1;CHECK(StatusAccess(7,0,&c,NULL)==BLE_ATT_ERR_INSUFFICIENT_RES);append_error=0;
  for(unsigned kind=0;kind<3;++kind) {
    status_stop_requested=false;status_task_handle=(void *)2;notify_enabled=true;
    mbuf_error=kind==1;notify_error=kind==2 ? 1:0;StatusTask(NULL);
  }
  CHECK(ble_diagnostics.notifications==1 && ble_diagnostics.notification_errors==2 && notifications==2);
  Encrypt(false);CHECK(!connection_pairing_authorized && !connection_encrypted && terminate_calls>0);
  CHECK(StatusAccess(7,0,&c,NULL)==BLE_ATT_ERR_READ_NOT_PERMITTED);
  Reset();CHECK(RobotBle_Start()==ESP_OK);Connect(true,false);Encrypt(true);
  RequestPairingResetFromButton();CHECK(pairing_reset_pending && queued_event);
  PairingResetHostEvent(queued_event);CHECK(!connection_pairing_authorized && !pairing_window_open && pairing_reset_retry_active && unpair_calls==0);
  struct ble_gap_event disconnect={.type=BLE_GAP_EVENT_DISCONNECT,.disconnect={.conn={.conn_handle=7}}};
  unpair_error=1;GapEvent(&disconnect,NULL);CHECK(pairing_reset_requested && !pairing_window_open && have_bond);
  unpair_error=0;ServicePairingResetRetry(NowMs()+200);CHECK(pairing_reset_pending);
  PairingResetHostEvent(queued_event);CHECK(!pairing_reset_requested && pairing_window_open && !have_bond && unpair_calls==2);
  CHECK(!IsPairingWindowOpen(pairing_window_deadline_ms));
  pairing_reset_requested=true;OnReset(1);CHECK(pairing_reset_requested && !host_ready && stop_requests>0);
  OnSync();CHECK(pairing_reset_pending);PairingResetHostEvent(queued_event);CHECK(pairing_window_open && !pairing_reset_requested);
  Reset();CHECK(RobotBle_Start()==ESP_OK);store_error=1;Connect(true,false);CHECK(!connection_pairing_authorized && terminate_calls==1);
  Reset();CHECK(RobotBle_Start()==ESP_OK);security_error=1;Connect(true,false);CHECK(!connection_pairing_authorized && terminate_calls==1);
  Reset();CHECK(RobotBle_Start()==ESP_OK);Connect(true,false);bonded=false;secured=true;
  struct ble_gap_event enc={.type=BLE_GAP_EVENT_ENC_CHANGE,.enc_change={.status=0,.conn_handle=7}};
  GapEvent(&enc,NULL);CHECK(!connection_encrypted && !connection_pairing_authorized && terminate_calls==1);
  Reset();CHECK(RobotBle_Start()==ESP_OK);Connect(true,false);Encrypt(true);HostTask(NULL);
  CHECK(!ble_started && !host_ready && host_stopping && stop_requests>0 && (event_bits & BLE_EVENT_HOST_EXIT));
  CHECK(RobotBle_Start()==ESP_ERR_INVALID_STATE);
  return 0;
}
