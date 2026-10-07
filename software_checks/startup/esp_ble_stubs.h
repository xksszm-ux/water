#pragma once
#include "esp_uart_start_stubs.h"
#undef ESP_LOGI
#undef ESP_LOGW
void MockBleLog(const char *,const char *,...);
#define ESP_LOGI(tag,...) MockBleLog(tag,__VA_ARGS__)
#define ESP_LOGW(tag,...) MockBleLog(tag,__VA_ARGS__)
#define ESP_LOGE(tag,...) MockBleLog(tag,__VA_ARGS__)
#define ESP_ERR_TIMEOUT -4
#define pdFALSE 0
#define configMAX_PRIORITIES 25
#define NIMBLE_HS_STACK_SIZE 4096
#define NIMBLE_CORE 0
#define NIMBLE_HOST_ENABLE_RPA 1
#define GPIO_NUM_0 0
#define GPIO_MODE_INPUT 1
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
#define BIT0 1U
#define BIT1 2U
#define BIT2 4U
#define BIT3 8U
#define BIT4 16U
typedef struct {uint64_t pin_bit_mask;int mode,pull_up_en,pull_down_en,intr_type;} gpio_config_t;
typedef struct {int unused;} StaticEventGroup_t;
typedef uint32_t EventBits_t;
typedef EventBits_t *EventGroupHandle_t;
typedef struct {uint8_t type;uint8_t val[6];} ble_addr_t;
typedef struct {uint8_t type;} ble_uuid_t;
typedef struct {ble_uuid_t u;uint8_t value[16];} ble_uuid128_t;
#define BLE_UUID128_INIT(...) {.u={.type=128},.value={__VA_ARGS__}}
#define BLE_HS_CONN_HANDLE_NONE 0xffff
enum { BLE_HS_EINVAL=1,BLE_HS_EALREADY,BLE_HS_ENOTCONN,BLE_HS_EAPP,
 BLE_ATT_ERR_WRITE_NOT_PERMITTED=10,BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN,
 BLE_ATT_ERR_UNLIKELY,BLE_ATT_ERR_VALUE_NOT_ALLOWED,BLE_ATT_ERR_READ_NOT_PERMITTED,
 BLE_ATT_ERR_INSUFFICIENT_RES, BLE_GATT_ACCESS_OP_WRITE_CHR=1,BLE_GATT_ACCESS_OP_READ_CHR=2,
 BLE_GATT_SVC_TYPE_PRIMARY=1, BLE_GATT_CHR_F_WRITE=1,BLE_GATT_CHR_F_WRITE_ENC=2,
 BLE_GATT_CHR_F_READ=4,BLE_GATT_CHR_F_NOTIFY=8,BLE_GATT_CHR_F_READ_ENC=16,
 BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC=32,BLE_HS_ADV_F_DISC_GEN=1,BLE_HS_ADV_F_BREDR_UNSUP=2,
 BLE_GAP_CONN_MODE_UND=1,BLE_GAP_DISC_MODE_GEN=1,BLE_HS_FOREVER=-1,
 BLE_OWN_ADDR_RANDOM=1,BLE_NPL_OK=0,BLE_ERR_REM_USER_CONN_TERM=19,
 BLE_GAP_EVENT_CONNECT=1,BLE_GAP_EVENT_DISCONNECT,BLE_GAP_EVENT_ENC_CHANGE,
 BLE_GAP_EVENT_REPEAT_PAIRING,BLE_GAP_EVENT_SUBSCRIBE,BLE_GAP_EVENT_ADV_COMPLETE,
 BLE_GAP_REPEAT_PAIRING_IGNORE=1,BLE_SM_IO_CAP_NO_IO=0,
 BLE_SM_PAIR_KEY_DIST_ENC=1,BLE_SM_PAIR_KEY_DIST_ID=2 };
struct os_mbuf {uint16_t length;uint8_t data[64];};
#define OS_MBUF_PKTLEN(m) ((m)->length)
struct ble_gatt_access_ctxt {uint8_t op;struct os_mbuf *om;};
typedef int (*ble_access_fn)(uint16_t,uint16_t,struct ble_gatt_access_ctxt *,void *);
struct ble_gatt_chr_def {const ble_uuid_t *uuid;ble_access_fn access_cb;uint16_t flags;uint16_t *val_handle;};
struct ble_gatt_svc_def {uint8_t type;const ble_uuid_t *uuid;const struct ble_gatt_chr_def *characteristics;};
struct ble_gap_conn_desc {uint16_t conn_handle;ble_addr_t peer_id_addr;struct {uint8_t encrypted,bonded;} sec_state;};
struct ble_gap_event {
 int type;
 union {
  struct {int status;uint16_t conn_handle;} connect;
  struct {struct ble_gap_conn_desc conn;} disconnect;
  struct {int status;uint16_t conn_handle;} enc_change;
  struct {uint16_t conn_handle;} repeat_pairing;
  struct {uint16_t conn_handle,attr_handle;uint8_t reason,prev_notify,cur_notify,prev_indicate,cur_indicate;} subscribe;
 };
};
struct ble_hs_adv_fields {uint8_t flags;ble_uuid128_t *uuids128;uint8_t num_uuids128,uuids128_is_complete;uint8_t *name;size_t name_len;uint8_t name_is_complete;};
struct ble_gap_adv_params {int conn_mode,disc_mode;};
struct ble_npl_event {void (*cb)(struct ble_npl_event *);};
struct ble_npl_callout {struct ble_npl_event event;};
struct ble_npl_eventq {int unused;};
typedef int ble_npl_error_t;
struct mock_ble_hs_cfg {void (*sync_cb)(void);void (*reset_cb)(int);uint8_t sm_io_cap,sm_bonding,sm_mitm,sm_sc,sm_sc_only,sm_our_key_dist,sm_their_key_dist;};
extern struct mock_ble_hs_cfg ble_hs_cfg;
EventGroupHandle_t xEventGroupCreateStatic(StaticEventGroup_t *);
EventBits_t xEventGroupClearBits(EventGroupHandle_t,EventBits_t);
EventBits_t xEventGroupSetBits(EventGroupHandle_t,EventBits_t);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t,EventBits_t,BaseType_t,BaseType_t,TickType_t);
BaseType_t xTaskCreate(void (*)(void *),const char *,unsigned,void *,unsigned,TaskHandle_t *);
esp_err_t gpio_config(const gpio_config_t *);
int gpio_get_level(int);
esp_err_t nvs_flash_init(void);
esp_err_t nimble_port_init(void);
esp_err_t nimble_port_deinit(void);
int nimble_port_stop(void);
void nimble_port_run(void);
struct ble_npl_eventq *nimble_port_get_dflt_eventq(void);
void ble_npl_event_init(struct ble_npl_event *,void (*)(struct ble_npl_event *),void *);
void ble_npl_event_deinit(struct ble_npl_event *);
void ble_npl_eventq_put(struct ble_npl_eventq *,struct ble_npl_event *);
int ble_npl_callout_init(struct ble_npl_callout *,struct ble_npl_eventq *,void (*)(struct ble_npl_event *),void *);
void ble_npl_callout_deinit(struct ble_npl_callout *);
void ble_npl_callout_stop(struct ble_npl_callout *);
int ble_npl_callout_reset(struct ble_npl_callout *,uint32_t);
uint32_t ble_npl_time_ms_to_ticks32(uint32_t);
int ble_addr_cmp(const ble_addr_t *,const ble_addr_t *);
int ble_store_util_bonded_peers(ble_addr_t *,int *,int);
int ble_gap_adv_stop(void);
int ble_gap_unpair(const ble_addr_t *);
int ble_hs_pvcy_rpa_config(int);
int ble_gap_terminate(uint16_t,int);
int ble_gap_conn_find(uint16_t,struct ble_gap_conn_desc *);
int ble_gap_security_initiate(uint16_t);
int ble_gap_adv_active(void);
int ble_gap_adv_set_fields(const struct ble_hs_adv_fields *);
int ble_gap_adv_rsp_set_fields(const struct ble_hs_adv_fields *);
int ble_gap_adv_start(uint8_t,const ble_addr_t *,int,const struct ble_gap_adv_params *,int (*)(struct ble_gap_event *,void *),void *);
int ble_hs_mbuf_to_flat(const struct os_mbuf *,void *,uint16_t,uint16_t *);
struct os_mbuf *ble_hs_mbuf_from_flat(const void *,uint16_t);
int os_mbuf_append(struct os_mbuf *,const void *,uint16_t);
int ble_gatts_notify_custom(uint16_t,uint16_t,struct os_mbuf *);
int ble_gatts_count_cfg(const struct ble_gatt_svc_def *);
int ble_gatts_add_svcs(const struct ble_gatt_svc_def *);
void ble_svc_gap_init(void);
void ble_svc_gatt_init(void);
int ble_svc_gap_device_name_set(const char *);
