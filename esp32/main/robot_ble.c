#include "robot_ble.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_pvcy.h"
#include "host/ble_store.h"
#include "host/ble_uuid.h"
#include "nimble/nimble_port.h"
#include "nvs_flash.h"
#include "os/os_mbuf.h"
#include "robot_ble_protocol.h"
#include "robot_protocol.h"
#include "robot_uart_link.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#define ROBOT_BLE_STATUS_PERIOD_MS       200U
#define ROBOT_BLE_STATUS_TASK_STACK      4096U
#define ROBOT_BLE_STATUS_TASK_PRIORITY   8U
#define ROBOT_BLE_SECURITY_TIMEOUT_MS    30000U
#define ROBOT_BLE_PAIR_BUTTON_GPIO       GPIO_NUM_0
#define ROBOT_BLE_PAIR_BUTTON_HOLD_MS    3000U
#define ROBOT_BLE_PAIRING_WINDOW_MS      60000U
#define ROBOT_BLE_START_TIMEOUT_MS        7000U
#define ROBOT_BLE_HOST_EXIT_TIMEOUT_MS    2000U
#define ROBOT_BLE_STATUS_EXIT_TIMEOUT_MS  1000U
#define ROBOT_BLE_STOP_TIMEOUT_MS          5000U
#define ROBOT_BLE_STOP_TASK_STACK          4096U
#define ROBOT_BLE_STOP_TASK_PRIORITY       9U
#define ROBOT_BLE_ADV_START_MAX_FAILURES  6U

#define BLE_EVENT_START_OK       BIT0
#define BLE_EVENT_START_FAIL     BIT1
#define BLE_EVENT_HOST_EXIT      BIT2
#define BLE_EVENT_STATUS_EXIT    BIT3
#define BLE_EVENT_STOP_DONE      BIT4
#define BLE_EVENT_ALL            (BLE_EVENT_START_OK | BLE_EVENT_START_FAIL | \
                                  BLE_EVENT_HOST_EXIT | BLE_EVENT_STATUS_EXIT | \
                                  BLE_EVENT_STOP_DONE)

/*
 * Canonical UUIDs:
 * service 7e57a000-bbcd-4b20-9f0d-3c8fa62e1000
 * command 7e57a000-bbcd-4b20-9f0d-3c8fa62e1001
 * status  7e57a000-bbcd-4b20-9f0d-3c8fa62e1002
 * BLE_UUID128_INIT stores the canonical UUID in little-endian byte order.
 */
static const ble_uuid128_t service_uuid = BLE_UUID128_INIT(
    0x00, 0x10, 0x2e, 0xa6, 0x8f, 0x3c, 0x0d, 0x9f,
    0x20, 0x4b, 0xcd, 0xbb, 0x00, 0xa0, 0x57, 0x7e);
static const ble_uuid128_t command_uuid = BLE_UUID128_INIT(
    0x01, 0x10, 0x2e, 0xa6, 0x8f, 0x3c, 0x0d, 0x9f,
    0x20, 0x4b, 0xcd, 0xbb, 0x00, 0xa0, 0x57, 0x7e);
static const ble_uuid128_t status_uuid = BLE_UUID128_INIT(
    0x02, 0x10, 0x2e, 0xa6, 0x8f, 0x3c, 0x0d, 0x9f,
    0x20, 0x4b, 0xcd, 0xbb, 0x00, 0xa0, 0x57, 0x7e);

static const char *TAG = "robot_ble";
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static RobotBleDiagnostics_t ble_diagnostics;
static StaticEventGroup_t lifecycle_event_storage;
static EventGroupHandle_t lifecycle_events;
static TaskHandle_t host_task_handle;
static TaskHandle_t status_task_handle;
static TaskHandle_t stop_task_handle;
static uint16_t status_value_handle;
static uint16_t connection_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t status_sequence;
static uint8_t own_address_type;
static bool ble_started;
static bool ble_starting;
static bool ble_faulted;
static bool nimble_initialized;
static bool host_ready;
static bool host_synced;
static bool host_stopping;
static bool status_stop_requested;
static bool lifecycle_operation_active;
static bool lifecycle_cleanup_failed;
static bool stop_wait_abandoned;
static esp_err_t stop_task_result;
static bool notify_enabled;
static bool connection_encrypted;
static bool connection_pairing_authorized;
static uint32_t encryption_deadline_ms;
static bool security_timeout_active;
static uint32_t pairing_window_deadline_ms;
static bool pairing_window_open;
static bool pairing_reset_requested;
static bool pairing_reset_pending;
static bool pairing_reset_retry_active;
static uint32_t pairing_reset_retry_deadline_ms;
static bool delete_bond_after_disconnect;
static struct ble_npl_event pairing_reset_event;
static bool pairing_reset_event_initialized;
static struct ble_npl_callout advertising_retry_callout;
static bool advertising_retry_callout_initialized;
static bool advertising_retry_pending;
static uint8_t advertising_failure_count;

/* Provided by the ESP-IDF NimBLE store implementation. */
void ble_store_config_init(void);

static int GapEvent(struct ble_gap_event *event, void *argument);
static int StartAdvertising(void);
static void RequestAdvertising(void);
static void AdvertisingRetryEvent(struct ble_npl_event *event);
static void StopTask(void *argument);
static esp_err_t StopLocked(void);

static uint32_t NowMs(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static bool DeadlineReached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static void IncrementCounter(uint32_t *counter)
{
    portENTER_CRITICAL(&state_lock);
    ++*counter;
    portEXIT_CRITICAL(&state_lock);
}

static bool BeginLifecycleOperation(void)
{
    bool acquired = false;

    portENTER_CRITICAL(&state_lock);
    if (!lifecycle_operation_active)
    {
        lifecycle_operation_active = true;
        acquired = true;
    }
    portEXIT_CRITICAL(&state_lock);
    return acquired;
}

static void EndLifecycleOperation(void)
{
    portENTER_CRITICAL(&state_lock);
    lifecycle_operation_active = false;
    portEXIT_CRITICAL(&state_lock);
}

static bool GetConnectionSnapshot(uint16_t *handle,
                                  bool *subscribed,
                                  bool *encrypted)
{
    bool connected;

    portENTER_CRITICAL(&state_lock);
    connected = connection_handle != BLE_HS_CONN_HANDLE_NONE;
    if (handle != NULL)
    {
        *handle = connection_handle;
    }
    if (subscribed != NULL)
    {
        *subscribed = notify_enabled;
    }
    if (encrypted != NULL)
    {
        *encrypted = connection_encrypted;
    }
    portEXIT_CRITICAL(&state_lock);
    return connected;
}

static bool IsCurrentConnection(uint16_t handle)
{
    bool current;

    portENTER_CRITICAL(&state_lock);
    current = (connection_handle != BLE_HS_CONN_HANDLE_NONE) &&
              (connection_handle == handle);
    portEXIT_CRITICAL(&state_lock);
    return current;
}

static bool IsConnectionAuthorized(uint16_t handle)
{
    bool authorized;

    portENTER_CRITICAL(&state_lock);
    authorized = (connection_handle == handle) &&
                 connection_pairing_authorized;
    portEXIT_CRITICAL(&state_lock);
    return authorized;
}

static bool IsHostReady(void)
{
    bool ready;

    portENTER_CRITICAL(&state_lock);
    ready = host_ready;
    portEXIT_CRITICAL(&state_lock);
    return ready;
}

static bool IsPairingWindowOpen(uint32_t now_ms)
{
    bool open;

    portENTER_CRITICAL(&state_lock);
    if (pairing_window_open &&
        DeadlineReached(now_ms, pairing_window_deadline_ms))
    {
        pairing_window_open = false;
    }
    open = pairing_window_open;
    portEXIT_CRITICAL(&state_lock);
    return open;
}

static int IsPeerBonded(const ble_addr_t *peer_id_addr, bool *bonded)
{
    ble_addr_t peers[1];
    int peer_count = 0;
    int rc;

    if ((peer_id_addr == NULL) || (bonded == NULL))
    {
        return BLE_HS_EINVAL;
    }
    *bonded = false;
    rc = ble_store_util_bonded_peers(peers, &peer_count, 1);
    if ((rc == 0) && (peer_count > 0))
    {
        *bonded = ble_addr_cmp(peer_id_addr, &peers[0]) == 0;
    }
    return rc;
}

static int DeleteBondAndOpenPairingWindow(uint32_t now_ms)
{
    ble_addr_t peers[1];
    int peer_count = 0;
    int rc = ble_gap_adv_stop();

    /*
     * Host-based privacy keeps a resolving-list entry in addition to the
     * persistent security records.  ble_gap_unpair() removes both, but it
     * deliberately refuses to run while advertising is active.
     */
    if ((rc != 0) && (rc != BLE_HS_EALREADY))
    {
        goto delete_failed;
    }

    rc = ble_store_util_bonded_peers(peers, &peer_count, 1);

    if ((rc == 0) && (peer_count > 0))
    {
        rc = ble_gap_unpair(&peers[0]);
    }
    if (rc == 0)
    {
        /*
         * When the last bond is removed, SMP_ID_RESET rotates the local IRK.
         * Generate a fresh RPA before advertising again so the old phone
         * cannot correlate the pre-reset address until the normal timeout.
         */
        rc = ble_hs_pvcy_rpa_config(NIMBLE_HOST_ENABLE_RPA);
    }
    if (rc == 0)
    {
        portENTER_CRITICAL(&state_lock);
        pairing_window_open = true;
        pairing_window_deadline_ms = now_ms + ROBOT_BLE_PAIRING_WINDOW_MS;
        ++ble_diagnostics.pairing_resets;
        portEXIT_CRITICAL(&state_lock);
        ESP_LOGW(TAG,
                 "Old phone bond cleared; pairing window open for %u seconds",
                 (unsigned int)(ROBOT_BLE_PAIRING_WINDOW_MS / 1000U));
        return 0;
    }

delete_failed:
    portENTER_CRITICAL(&state_lock);
    pairing_window_open = false;
    ++ble_diagnostics.pairing_reset_errors;
    portEXIT_CRITICAL(&state_lock);
    ESP_LOGE(TAG, "Unable to clear old phone bond, rc=%d", rc);
    return rc;
}

static bool FinalizePairingReset(uint32_t now_ms)
{
    const int rc = DeleteBondAndOpenPairingWindow(now_ms);

    portENTER_CRITICAL(&state_lock);
    if (rc == 0)
    {
        pairing_reset_requested = false;
        delete_bond_after_disconnect = false;
        pairing_reset_retry_active = false;
    }
    else
    {
        /* Keep the physical request latched until unpair really succeeds. */
        pairing_reset_requested = true;
        pairing_reset_retry_active = true;
        pairing_reset_retry_deadline_ms = now_ms + 200U;
    }
    portEXIT_CRITICAL(&state_lock);

    if (rc == 0)
    {
        /* ble_gap_adv_stop() does not emit ADV_COMPLETE; restart explicitly. */
        RequestAdvertising();
        return true;
    }
    return false;
}

static void PairingResetHostEvent(struct ble_npl_event *event)
{
    uint16_t handle;
    bool connected;
    bool requested;
    int rc;

    (void)event;
    (void)RobotUartLink_RequestStop();

    portENTER_CRITICAL(&state_lock);
    pairing_reset_pending = false;
    pairing_reset_retry_active = false;
    requested = pairing_reset_requested;
    if (requested)
    {
        pairing_window_open = false;
        connected = connection_handle != BLE_HS_CONN_HANDLE_NONE;
        handle = connection_handle;
        delete_bond_after_disconnect = connected;
        connection_pairing_authorized = false;
        connection_encrypted = false;
        notify_enabled = false;
        security_timeout_active = false;
    }
    else
    {
        connected = false;
        handle = BLE_HS_CONN_HANDLE_NONE;
    }
    portEXIT_CRITICAL(&state_lock);

    /* A disconnect callback may have completed while this retry was queued. */
    if (!requested)
    {
        return;
    }

    if (connected)
    {
        rc = ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
        if (rc == BLE_HS_ENOTCONN)
        {
            portENTER_CRITICAL(&state_lock);
            connection_handle = BLE_HS_CONN_HANDLE_NONE;
            delete_bond_after_disconnect = false;
            pairing_reset_retry_active = false;
            portEXIT_CRITICAL(&state_lock);
            (void)FinalizePairingReset(NowMs());
        }
        else
        {
            portENTER_CRITICAL(&state_lock);
            /*
             * Keep the physical reset request latched until DISCONNECT.  A
             * 200 ms watchdog also covers a transient terminate failure or
             * a missing disconnect event without touching the bond store
             * outside the NimBLE host context.
             */
            delete_bond_after_disconnect = true;
            pairing_reset_retry_active = true;
            pairing_reset_retry_deadline_ms = NowMs() + 200U;
            if ((rc != 0) && (rc != BLE_HS_EALREADY))
            {
                ++ble_diagnostics.pairing_reset_errors;
            }
            portEXIT_CRITICAL(&state_lock);
            if ((rc != 0) && (rc != BLE_HS_EALREADY))
            {
                ESP_LOGE(TAG,
                         "Unable to disconnect before bond reset, rc=%d; retry armed",
                         rc);
            }
        }
    }
    else
    {
        (void)FinalizePairingReset(NowMs());
    }
}

static void RequestPairingResetFromButton(void)
{
    bool queue_event = false;

    (void)RobotUartLink_RequestStop();
    portENTER_CRITICAL(&state_lock);
    pairing_reset_requested = true;
    if (!pairing_reset_pending && !pairing_reset_retry_active)
    {
        pairing_reset_pending = true;
        queue_event = true;
    }
    portEXIT_CRITICAL(&state_lock);

    if (queue_event)
    {
        ble_npl_eventq_put(nimble_port_get_dflt_eventq(),
                           &pairing_reset_event);
        ESP_LOGW(TAG,
                 "BOOT held for 3 seconds; STOP requested and bond reset queued");
    }
}

static void ServicePairingResetRetry(uint32_t now_ms)
{
    bool queue_event = false;

    portENTER_CRITICAL(&state_lock);
    if (pairing_reset_requested && pairing_reset_retry_active &&
        DeadlineReached(now_ms, pairing_reset_retry_deadline_ms) &&
        !pairing_reset_pending)
    {
        pairing_reset_retry_active = false;
        pairing_reset_pending = true;
        queue_event = true;
    }
    portEXIT_CRITICAL(&state_lock);

    if (queue_event)
    {
        ble_npl_eventq_put(nimble_port_get_dflt_eventq(),
                           &pairing_reset_event);
    }
}

static void TerminateConnectionWithRetry(uint16_t handle, uint32_t now_ms)
{
    const int rc = ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);

    if ((rc != 0) && (rc != BLE_HS_EALREADY) &&
        (rc != BLE_HS_ENOTCONN) && IsCurrentConnection(handle))
    {
        portENTER_CRITICAL(&state_lock);
        security_timeout_active = true;
        encryption_deadline_ms = now_ms + 200U;
        portEXIT_CRITICAL(&state_lock);
        ESP_LOGE(TAG, "BLE disconnect request failed, rc=%d; retry armed", rc);
    }
}

static void ServiceSecurityTimeout(uint32_t now_ms)
{
    uint16_t handle = BLE_HS_CONN_HANDLE_NONE;
    bool terminate = false;

    portENTER_CRITICAL(&state_lock);
    if ((connection_handle != BLE_HS_CONN_HANDLE_NONE) &&
        !connection_encrypted && security_timeout_active &&
        DeadlineReached(now_ms, encryption_deadline_ms))
    {
        handle = connection_handle;
        security_timeout_active = false;
        ++ble_diagnostics.encryption_failures;
        terminate = true;
    }
    portEXIT_CRITICAL(&state_lock);

    if (terminate)
    {
        (void)RobotUartLink_RequestStop();
        ESP_LOGE(TAG, "BLE encryption timed out; disconnecting phone");
        TerminateConnectionWithRetry(handle, now_ms);
    }
}

static uint16_t NextStatusSequence(void)
{
    uint16_t sequence;

    portENTER_CRITICAL(&state_lock);
    sequence = status_sequence++;
    portEXIT_CRITICAL(&state_lock);
    return sequence;
}

static size_t BuildStatusPacket(uint8_t output[ROBOT_BLE_STATUS_SIZE])
{
    RobotBleStatusFields_t fields = {
        .link_flags = 0U,
        .valid_flags = 0U,
        .battery_mv = ROBOT_PROTOCOL_INVALID_U16,
        .left_pwm = 0,
        .right_pwm = 0,
        .distance_mm = ROBOT_PROTOCOL_INVALID_U16,
        .mode = 0xFFU,
        .error_status = 0xFFU,
        .owner = ROBOT_PROTOCOL_OWNER_NONE
    };
    RobotProtocolStatus_t uart_status;
    uint32_t age_ms = 0U;

    bool encrypted = false;
    if (GetConnectionSnapshot(NULL, NULL, &encrypted))
    {
        fields.link_flags |= ROBOT_BLE_LINK_CONNECTED;
    }
    if (encrypted)
    {
        fields.link_flags |= ROBOT_BLE_LINK_ENCRYPTED;
    }
    if (RobotUartLink_IsOnline())
    {
        fields.link_flags |= ROBOT_BLE_LINK_UART_ONLINE;
    }
    if (RobotUartLink_GetStatus(&uart_status, &age_ms))
    {
        (void)age_ms;
        fields.link_flags |= ROBOT_BLE_LINK_STATUS_FRESH;
        fields.valid_flags = uart_status.valid_flags;
        fields.battery_mv = uart_status.battery_mv;
        fields.left_pwm = uart_status.applied_left_pwm;
        fields.right_pwm = uart_status.applied_right_pwm;
        fields.distance_mm = uart_status.distance_mm;
        fields.mode = uart_status.mode;
        fields.error_status = uart_status.error_status;
        fields.owner = uart_status.owner;
    }

    return RobotBleProtocol_EncodeStatus(NextStatusSequence(), &fields,
                                         output, ROBOT_BLE_STATUS_SIZE);
}

static int CommandAccess(uint16_t conn_handle,
                         uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *context,
                         void *argument)
{
    uint8_t raw[ROBOT_BLE_COMMAND_SIZE];
    uint16_t length = 0U;
    RobotBleCommand_t command;
    RobotBleDecodeResult_t result;
    int rc;

    (void)attr_handle;
    (void)argument;

    if ((context == NULL) ||
        (context->op != BLE_GATT_ACCESS_OP_WRITE_CHR) ||
        !IsCurrentConnection(conn_handle) ||
        !IsConnectionAuthorized(conn_handle) ||
        (conn_handle == BLE_HS_CONN_HANDLE_NONE))
    {
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }
    if (OS_MBUF_PKTLEN(context->om) != ROBOT_BLE_COMMAND_SIZE)
    {
        IncrementCounter(&ble_diagnostics.rejected_commands);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    rc = ble_hs_mbuf_to_flat(context->om, raw, sizeof(raw), &length);
    if ((rc != 0) || (length != ROBOT_BLE_COMMAND_SIZE))
    {
        IncrementCounter(&ble_diagnostics.rejected_commands);
        return BLE_ATT_ERR_UNLIKELY;
    }

    result = RobotBleProtocol_DecodeCommand(raw, length, &command);
    if (result != ROBOT_BLE_DECODE_OK)
    {
        IncrementCounter(&ble_diagnostics.rejected_commands);
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    }

    if (command.opcode != ROBOT_BLE_OPCODE_STOP)
    {
        /* Step 10A deliberately has no path to non-zero motor output. */
        IncrementCounter(&ble_diagnostics.rejected_commands);
        (void)RobotUartLink_RequestStop();
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }

    if (RobotUartLink_RequestStop() != ESP_OK)
    {
        IncrementCounter(&ble_diagnostics.rejected_commands);
        return BLE_ATT_ERR_UNLIKELY;
    }

    IncrementCounter(&ble_diagnostics.stop_commands);
    ESP_LOGI(TAG, "BLE STOP accepted, phone_seq=%u",
             (unsigned int)command.sequence);
    return 0;
}

static int StatusAccess(uint16_t conn_handle,
                        uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *context,
                        void *argument)
{
    uint8_t packet[ROBOT_BLE_STATUS_SIZE];
    size_t length;

    (void)attr_handle;
    (void)argument;

    if ((context == NULL) ||
        (context->op != BLE_GATT_ACCESS_OP_READ_CHR) ||
        !IsCurrentConnection(conn_handle) ||
        !IsConnectionAuthorized(conn_handle))
    {
        return BLE_ATT_ERR_READ_NOT_PERMITTED;
    }

    length = BuildStatusPacket(packet);
    if (length != ROBOT_BLE_STATUS_SIZE)
    {
        return BLE_ATT_ERR_UNLIKELY;
    }
    return (os_mbuf_append(context->om, packet, (uint16_t)length) == 0) ?
        0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static const struct ble_gatt_svc_def services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &command_uuid.u,
                .access_cb = CommandAccess,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC
            },
            {
                .uuid = &status_uuid.u,
                .access_cb = StatusAccess,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY |
                         BLE_GATT_CHR_F_READ_ENC |
                         BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC,
                .val_handle = &status_value_handle
            },
            { 0 }
        }
    },
    { 0 }
};

static int StartAdvertising(void)
{
    struct ble_hs_adv_fields fields;
    struct ble_hs_adv_fields response_fields;
    struct ble_gap_adv_params parameters;
    int rc;

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&service_uuid;
    fields.num_uuids128 = 1U;
    fields.uuids128_is_complete = 1U;
    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0)
    {
        return rc;
    }

    memset(&response_fields, 0, sizeof(response_fields));
    response_fields.name = (uint8_t *)ROBOT_BLE_DEVICE_NAME;
    response_fields.name_len = strlen(ROBOT_BLE_DEVICE_NAME);
    response_fields.name_is_complete = 1U;
    rc = ble_gap_adv_rsp_set_fields(&response_fields);
    if (rc != 0)
    {
        return rc;
    }

    memset(&parameters, 0, sizeof(parameters));
    parameters.conn_mode = BLE_GAP_CONN_MODE_UND;
    parameters.disc_mode = BLE_GAP_DISC_MODE_GEN;
    return ble_gap_adv_start(own_address_type, NULL, BLE_HS_FOREVER,
                             &parameters, GapEvent, NULL);
}

static void SignalStartupFailure(void)
{
    bool signal;

    portENTER_CRITICAL(&state_lock);
    signal = ble_starting && !host_ready && !host_stopping;
    portEXIT_CRITICAL(&state_lock);
    if (signal && (lifecycle_events != NULL))
    {
        (void)xEventGroupSetBits(lifecycle_events, BLE_EVENT_START_FAIL);
    }
}

static void MarkAdvertisingStarted(void)
{
    bool signal_start;

    portENTER_CRITICAL(&state_lock);
    advertising_retry_pending = false;
    advertising_failure_count = 0U;
    host_ready = true;
    signal_start = ble_starting && !host_stopping;
    portEXIT_CRITICAL(&state_lock);

    if (signal_start && (lifecycle_events != NULL))
    {
        (void)xEventGroupSetBits(lifecycle_events, BLE_EVENT_START_OK);
    }
    ESP_LOGI(TAG, "Advertising as %s (Step 10A STOP-only)",
             ROBOT_BLE_DEVICE_NAME);
}

static void ScheduleAdvertisingRetry(int error_code)
{
    static const uint32_t retry_delays_ms[] = {
        100U, 250U, 500U, 1000U, 2000U
    };
    uint8_t failure_count;
    uint32_t delay_ms;
    bool startup_failed;
    ble_npl_error_t rc;

    portENTER_CRITICAL(&state_lock);
    ++ble_diagnostics.advertising_errors;
    if (advertising_failure_count < UINT8_MAX)
    {
        ++advertising_failure_count;
    }
    failure_count = advertising_failure_count;
    startup_failed = ble_starting && !host_ready &&
                     (failure_count >= ROBOT_BLE_ADV_START_MAX_FAILURES);
    portEXIT_CRITICAL(&state_lock);

    ESP_LOGE(TAG, "BLE advertising failed, rc=%d, failure=%u",
             error_code, (unsigned int)failure_count);
    if (startup_failed)
    {
        SignalStartupFailure();
        return;
    }

    const size_t delay_index =
        (failure_count == 0U) ? 0U : (size_t)(failure_count - 1U);
    delay_ms = retry_delays_ms[
        (delay_index < (sizeof(retry_delays_ms) /
                        sizeof(retry_delays_ms[0]))) ?
        delay_index : ((sizeof(retry_delays_ms) /
                        sizeof(retry_delays_ms[0])) - 1U)];
    rc = ble_npl_callout_reset(
        &advertising_retry_callout,
        ble_npl_time_ms_to_ticks32(delay_ms));
    if (rc != BLE_NPL_OK)
    {
        IncrementCounter(&ble_diagnostics.lifecycle_errors);
        ESP_LOGE(TAG, "Unable to schedule advertising retry, rc=%d", rc);
        SignalStartupFailure();
        return;
    }

    portENTER_CRITICAL(&state_lock);
    advertising_retry_pending = true;
    ++ble_diagnostics.advertising_retries;
    portEXIT_CRITICAL(&state_lock);
}

static void AdvertisingRetryEvent(struct ble_npl_event *event)
{
    bool allowed;
    int rc;

    (void)event;
    portENTER_CRITICAL(&state_lock);
    advertising_retry_pending = false;
    allowed = host_synced && !host_stopping &&
              (connection_handle == BLE_HS_CONN_HANDLE_NONE);
    portEXIT_CRITICAL(&state_lock);
    if (!allowed)
    {
        return;
    }

    if (ble_gap_adv_active() != 0)
    {
        MarkAdvertisingStarted();
        return;
    }

    rc = StartAdvertising();
    if (rc == 0)
    {
        MarkAdvertisingStarted();
    }
    else
    {
        ScheduleAdvertisingRetry(rc);
    }
}

static void RequestAdvertising(void)
{
    bool allowed;
    ble_npl_error_t rc;

    portENTER_CRITICAL(&state_lock);
    allowed = host_synced && !host_stopping &&
              (connection_handle == BLE_HS_CONN_HANDLE_NONE);
    if (allowed)
    {
        advertising_failure_count = 0U;
    }
    portEXIT_CRITICAL(&state_lock);
    if (!allowed)
    {
        return;
    }

    rc = ble_npl_callout_reset(&advertising_retry_callout, 1U);
    if (rc != BLE_NPL_OK)
    {
        IncrementCounter(&ble_diagnostics.lifecycle_errors);
        ESP_LOGE(TAG, "Unable to queue advertising start, rc=%d", rc);
        SignalStartupFailure();
        return;
    }
    portENTER_CRITICAL(&state_lock);
    advertising_retry_pending = true;
    portEXIT_CRITICAL(&state_lock);
}

static void OnSync(void)
{
    const int rc = ble_hs_pvcy_rpa_config(NIMBLE_HOST_ENABLE_RPA);

    if (rc != 0)
    {
        portENTER_CRITICAL(&state_lock);
        host_ready = false;
        host_synced = false;
        ++ble_diagnostics.lifecycle_errors;
        portEXIT_CRITICAL(&state_lock);
        ESP_LOGE(TAG, "BLE identity setup failed, rc=%d", rc);
        (void)RobotUartLink_RequestStop();
        SignalStartupFailure();
    }
    else
    {
        /* Classic ESP32 uses host-based privacy through the random-address path. */
        own_address_type = BLE_OWN_ADDR_RANDOM;
        portENTER_CRITICAL(&state_lock);
        host_synced = true;
        portEXIT_CRITICAL(&state_lock);
        RequestAdvertising();
    }
}

static void OnReset(int reason)
{
    if (advertising_retry_callout_initialized)
    {
        ble_npl_callout_stop(&advertising_retry_callout);
    }
    portENTER_CRITICAL(&state_lock);
    connection_handle = BLE_HS_CONN_HANDLE_NONE;
    host_ready = false;
    host_synced = false;
    advertising_retry_pending = false;
    advertising_failure_count = 0U;
    notify_enabled = false;
    connection_encrypted = false;
    connection_pairing_authorized = false;
    encryption_deadline_ms = 0U;
    security_timeout_active = false;
    pairing_window_open = false;
    pairing_reset_requested = false;
    pairing_reset_pending = false;
    pairing_reset_retry_active = false;
    delete_bond_after_disconnect = false;
    ++ble_diagnostics.host_resets;
    portEXIT_CRITICAL(&state_lock);
    (void)RobotUartLink_RequestStop();
    ESP_LOGE(TAG, "NimBLE host reset, reason=%d; STOP requested", reason);
}

static int GapEvent(struct ble_gap_event *event, void *argument)
{
    (void)argument;

    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0)
        {
            struct ble_gap_conn_desc descriptor;
            const uint32_t now_ms = NowMs();
            bool bonded = false;
            bool authorized = false;
            int rc = ble_gap_conn_find(event->connect.conn_handle,
                                       &descriptor);

            if (rc == 0)
            {
                rc = IsPeerBonded(&descriptor.peer_id_addr, &bonded);
            }
            if (rc == 0)
            {
                authorized = bonded || IsPairingWindowOpen(now_ms);
            }
            else
            {
                IncrementCounter(&ble_diagnostics.pairing_reset_errors);
            }

            portENTER_CRITICAL(&state_lock);
            connection_handle = event->connect.conn_handle;
            notify_enabled = false;
            connection_encrypted = false;
            connection_pairing_authorized = authorized;
            encryption_deadline_ms = now_ms + ROBOT_BLE_SECURITY_TIMEOUT_MS;
            security_timeout_active = authorized;
            ++ble_diagnostics.connections;
            portEXIT_CRITICAL(&state_lock);
            (void)RobotUartLink_RequestStop();

            if (!authorized)
            {
                ESP_LOGW(TAG,
                         "Unbonded phone rejected; hold BOOT 3 seconds first");
                TerminateConnectionWithRetry(event->connect.conn_handle,
                                             now_ms);
                break;
            }

            ESP_LOGI(TAG, "Authorized phone connected; safety STOP requested");
            rc = ble_gap_security_initiate(event->connect.conn_handle);
            if ((rc != 0) && (rc != BLE_HS_EALREADY))
            {
                ESP_LOGE(TAG, "Unable to initiate encrypted BLE link, rc=%d",
                         rc);
                TerminateConnectionWithRetry(event->connect.conn_handle,
                                             now_ms);
            }
        }
        else
        {
            RequestAdvertising();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
    {
        bool clear_bond;

        if (!IsCurrentConnection(event->disconnect.conn.conn_handle))
        {
            break;
        }
        portENTER_CRITICAL(&state_lock);
        connection_handle = BLE_HS_CONN_HANDLE_NONE;
        notify_enabled = false;
        connection_encrypted = false;
        connection_pairing_authorized = false;
        encryption_deadline_ms = 0U;
        security_timeout_active = false;
        clear_bond = delete_bond_after_disconnect;
        delete_bond_after_disconnect = false;
        pairing_reset_retry_active = false;
        ++ble_diagnostics.disconnections;
        portEXIT_CRITICAL(&state_lock);
        (void)RobotUartLink_RequestStop();
        ESP_LOGW(TAG, "Phone disconnected; safety STOP requested");
        if (clear_bond)
        {
            (void)FinalizePairingReset(NowMs());
        }
        else
        {
            RequestAdvertising();
        }
        break;
    }

    case BLE_GAP_EVENT_ENC_CHANGE:
    {
        struct ble_gap_conn_desc descriptor;
        const bool authorized =
            IsConnectionAuthorized(event->enc_change.conn_handle);
        if (!IsCurrentConnection(event->enc_change.conn_handle))
        {
            break;
        }
        const bool encrypted =
            (event->enc_change.status == 0) &&
            (ble_gap_conn_find(event->enc_change.conn_handle,
                               &descriptor) == 0) &&
            (descriptor.sec_state.encrypted != 0U) &&
            (descriptor.sec_state.bonded != 0U) && authorized;

        portENTER_CRITICAL(&state_lock);
        connection_encrypted = encrypted;
        security_timeout_active = false;
        if (encrypted)
        {
            pairing_window_open = false;
        }
        if (!encrypted)
        {
            ++ble_diagnostics.encryption_failures;
        }
        portEXIT_CRITICAL(&state_lock);
        if (!encrypted)
        {
            (void)RobotUartLink_RequestStop();
            ESP_LOGE(TAG,
                     "BLE encryption failed or was lost; disconnecting phone");
            TerminateConnectionWithRetry(event->enc_change.conn_handle,
                                         NowMs());
        }
        else
        {
            ESP_LOGI(TAG, "BLE link encrypted%s",
                     descriptor.sec_state.bonded ? " and bonded" : "");
        }
        break;
    }

    case BLE_GAP_EVENT_REPEAT_PAIRING:
    {
        struct ble_gap_conn_desc descriptor;
        int rc;

        (void)RobotUartLink_RequestStop();
        if (IsPairingWindowOpen(NowMs()) &&
            (ble_gap_conn_find(event->repeat_pairing.conn_handle,
                               &descriptor) == 0))
        {
            /*
             * unpair() clears the security store and the host resolving list.
             * It also starts disconnecting this old-key connection.  Pairing
             * must restart on a fresh connection, not inside this callback.
             */
            rc = ble_gap_unpair(&descriptor.peer_id_addr);
            if (rc == 0)
            {
                portENTER_CRITICAL(&state_lock);
                pairing_reset_requested = true;
                delete_bond_after_disconnect = true;
                portEXIT_CRITICAL(&state_lock);
                ESP_LOGW(TAG,
                         "Old peer keys removed; refreshing RPA after disconnect");
                TerminateConnectionWithRetry(event->repeat_pairing.conn_handle,
                                             NowMs());
                return BLE_GAP_REPEAT_PAIRING_IGNORE;
            }
            IncrementCounter(&ble_diagnostics.pairing_reset_errors);
        }
        TerminateConnectionWithRetry(event->repeat_pairing.conn_handle,
                                     NowMs());
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    }

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG,
                 "BLE subscribe: conn=%u attr=%u status_attr=%u reason=%u "
                 "notify=%u->%u indicate=%u->%u",
                 (unsigned int)event->subscribe.conn_handle,
                 (unsigned int)event->subscribe.attr_handle,
                 (unsigned int)status_value_handle,
                 (unsigned int)event->subscribe.reason,
                 (unsigned int)event->subscribe.prev_notify,
                 (unsigned int)event->subscribe.cur_notify,
                 (unsigned int)event->subscribe.prev_indicate,
                 (unsigned int)event->subscribe.cur_indicate);
        if ((event->subscribe.attr_handle == status_value_handle) &&
            IsCurrentConnection(event->subscribe.conn_handle))
        {
            portENTER_CRITICAL(&state_lock);
            notify_enabled = event->subscribe.cur_notify != 0U;
            portEXIT_CRITICAL(&state_lock);
            if (event->subscribe.cur_notify == 0U)
            {
                (void)RobotUartLink_RequestStop();
            }
        }
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        RequestAdvertising();
        break;

    default:
        break;
    }
    return 0;
}

static void HostTask(void *argument)
{
    TaskHandle_t status_task;
    bool startup_incomplete;
    bool cleanup_ok = true;

    (void)argument;
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    (void)RobotUartLink_RequestStop();

    if (advertising_retry_callout_initialized)
    {
        ble_npl_callout_stop(&advertising_retry_callout);
    }
    portENTER_CRITICAL(&state_lock);
    host_ready = false;
    host_synced = false;
    host_stopping = true;
    status_stop_requested = true;
    status_task = status_task_handle;
    startup_incomplete = ble_starting;
    connection_handle = BLE_HS_CONN_HANDLE_NONE;
    notify_enabled = false;
    connection_encrypted = false;
    connection_pairing_authorized = false;
    encryption_deadline_ms = 0U;
    security_timeout_active = false;
    pairing_window_open = false;
    pairing_reset_requested = false;
    pairing_reset_pending = false;
    pairing_reset_retry_active = false;
    delete_bond_after_disconnect = false;
    advertising_retry_pending = false;
    portEXIT_CRITICAL(&state_lock);

    if (status_task != NULL)
    {
        xTaskNotifyGive(status_task);
        const EventBits_t status_bits = xEventGroupWaitBits(
            lifecycle_events, BLE_EVENT_STATUS_EXIT, pdFALSE, pdFALSE,
            pdMS_TO_TICKS(ROBOT_BLE_STATUS_EXIT_TIMEOUT_MS));
        if ((status_bits & BLE_EVENT_STATUS_EXIT) == 0U)
        {
            /*
             * Do not force-delete a task that may hold a NimBLE lock.  Keep
             * the stack allocated and require a reboot instead of pretending
             * that deinitialization is safe.
             */
            cleanup_ok = false;
            portENTER_CRITICAL(&state_lock);
            lifecycle_cleanup_failed = true;
            ble_faulted = true;
            ++ble_diagnostics.lifecycle_errors;
            portEXIT_CRITICAL(&state_lock);
            ESP_LOGE(TAG,
                     "BLE status task did not exit; deinit and restart blocked");
        }
    }

    if (cleanup_ok && advertising_retry_callout_initialized)
    {
        ble_npl_callout_deinit(&advertising_retry_callout);
        advertising_retry_callout_initialized = false;
    }
    if (cleanup_ok && pairing_reset_event_initialized)
    {
        ble_npl_event_deinit(&pairing_reset_event);
        pairing_reset_event_initialized = false;
    }

    portENTER_CRITICAL(&state_lock);
    host_task_handle = NULL;
    ble_started = false;
    ble_starting = false;
    portEXIT_CRITICAL(&state_lock);
    if (startup_incomplete)
    {
        (void)xEventGroupSetBits(lifecycle_events, BLE_EVENT_START_FAIL);
    }
    (void)xEventGroupSetBits(lifecycle_events, BLE_EVENT_HOST_EXIT);
    ESP_LOGW(TAG, "NimBLE host task exited; lifecycle closed");
    vTaskDelete(NULL);
}

static void StatusTask(void *argument)
{
    uint8_t packet[ROBOT_BLE_STATUS_SIZE];
    uint32_t button_pressed_at_ms = 0U;
    bool button_pressed = false;
    bool button_action_latched = false;

    (void)argument;
    for (;;)
    {
        bool stop_requested;

        portENTER_CRITICAL(&state_lock);
        stop_requested = status_stop_requested;
        portEXIT_CRITICAL(&state_lock);
        if (stop_requested)
        {
            break;
        }

        const uint32_t now_ms = NowMs();
        uint16_t handle;
        bool subscribed;

        if (gpio_get_level(ROBOT_BLE_PAIR_BUTTON_GPIO) == 0)
        {
            if (!button_pressed)
            {
                button_pressed = true;
                button_pressed_at_ms = now_ms;
            }
            else if (!button_action_latched &&
                     ((uint32_t)(now_ms - button_pressed_at_ms) >=
                      ROBOT_BLE_PAIR_BUTTON_HOLD_MS))
            {
                button_action_latched = true;
                RequestPairingResetFromButton();
            }
        }
        else
        {
            button_pressed = false;
            button_action_latched = false;
        }

        (void)IsPairingWindowOpen(now_ms);
        ServicePairingResetRetry(now_ms);
        ServiceSecurityTimeout(now_ms);

        bool encrypted;
        if (IsHostReady() &&
            GetConnectionSnapshot(&handle, &subscribed, &encrypted) &&
            subscribed && encrypted)
        {
            const size_t length = BuildStatusPacket(packet);
            struct os_mbuf *buffer = NULL;
            int rc = BLE_HS_EAPP;

            if (length == ROBOT_BLE_STATUS_SIZE)
            {
                buffer = ble_hs_mbuf_from_flat(packet, (uint16_t)length);
                if (buffer != NULL)
                {
                    rc = ble_gatts_notify_custom(handle,
                                                 status_value_handle,
                                                 buffer);
                }
            }
            if (rc == 0)
            {
                IncrementCounter(&ble_diagnostics.notifications);
            }
            else
            {
                IncrementCounter(&ble_diagnostics.notification_errors);
            }
        }

        (void)ulTaskNotifyTake(pdTRUE,
                               pdMS_TO_TICKS(ROBOT_BLE_STATUS_PERIOD_MS));
    }

    portENTER_CRITICAL(&state_lock);
    status_task_handle = NULL;
    portEXIT_CRITICAL(&state_lock);
    (void)xEventGroupSetBits(lifecycle_events, BLE_EVENT_STATUS_EXIT);
    vTaskDelete(NULL);
}

static void StopTask(void *argument)
{
    TaskHandle_t host_task;
    esp_err_t result = ESP_OK;
    int nimble_stop_result = 0;
    bool cleanup_failed;
    bool abandoned;

    (void)argument;
    portENTER_CRITICAL(&state_lock);
    host_task = host_task_handle;
    portEXIT_CRITICAL(&state_lock);

    if (host_task != NULL)
    {
        /* IDF 5.4.4 can wait forever internally; isolate it in this worker. */
        nimble_stop_result = nimble_port_stop();
        if (nimble_stop_result != 0)
        {
            result = ESP_FAIL;
        }
    }

    if (result == ESP_OK)
    {
        const EventBits_t exit_bits = xEventGroupWaitBits(
            lifecycle_events, BLE_EVENT_HOST_EXIT, pdFALSE, pdFALSE,
            pdMS_TO_TICKS(ROBOT_BLE_HOST_EXIT_TIMEOUT_MS));
        if ((exit_bits & BLE_EVENT_HOST_EXIT) == 0U)
        {
            result = ESP_ERR_TIMEOUT;
        }
    }

    portENTER_CRITICAL(&state_lock);
    cleanup_failed = lifecycle_cleanup_failed;
    portEXIT_CRITICAL(&state_lock);
    if ((result == ESP_OK) && cleanup_failed)
    {
        result = ESP_FAIL;
    }
    if (result == ESP_OK)
    {
        result = nimble_port_deinit();
    }

    portENTER_CRITICAL(&state_lock);
    abandoned = stop_wait_abandoned;
    stop_task_result = result;
    stop_task_handle = NULL;
    if (result == ESP_OK)
    {
        nimble_initialized = false;
        host_stopping = false;
        if (!abandoned)
        {
            ble_faulted = false;
        }
    }
    if ((result != ESP_OK) || abandoned)
    {
        ble_faulted = true;
        ++ble_diagnostics.lifecycle_errors;
    }
    portEXIT_CRITICAL(&state_lock);

    if (result != ESP_OK)
    {
        ESP_LOGE(TAG, "NimBLE stop/deinit failed, rc=%d, host_rc=%d",
                 (int)result, nimble_stop_result);
    }
    (void)xEventGroupSetBits(lifecycle_events, BLE_EVENT_STOP_DONE);
    vTaskDelete(NULL);
}

static esp_err_t StopLocked(void)
{
    bool initialized;
    bool worker_active;

    portENTER_CRITICAL(&state_lock);
    initialized = nimble_initialized;
    worker_active = stop_task_handle != NULL;
    if (initialized && !worker_active)
    {
        host_stopping = true;
        host_ready = false;
        stop_wait_abandoned = false;
        stop_task_result = ESP_FAIL;
    }
    portEXIT_CRITICAL(&state_lock);
    if (!initialized || worker_active)
    {
        return ESP_ERR_INVALID_STATE;
    }

    (void)RobotUartLink_RequestStop();
    (void)xEventGroupClearBits(lifecycle_events, BLE_EVENT_STOP_DONE);
    if (xTaskCreate(StopTask, "nimble_stop",
                    ROBOT_BLE_STOP_TASK_STACK, NULL,
                    ROBOT_BLE_STOP_TASK_PRIORITY,
                    &stop_task_handle) != pdPASS)
    {
        portENTER_CRITICAL(&state_lock);
        ble_faulted = true;
        ++ble_diagnostics.lifecycle_errors;
        portEXIT_CRITICAL(&state_lock);
        return ESP_ERR_NO_MEM;
    }

    const EventBits_t stop_bits = xEventGroupWaitBits(
        lifecycle_events, BLE_EVENT_STOP_DONE, pdFALSE, pdFALSE,
        pdMS_TO_TICKS(ROBOT_BLE_STOP_TIMEOUT_MS));
    if ((stop_bits & BLE_EVENT_STOP_DONE) == 0U)
    {
        portENTER_CRITICAL(&state_lock);
        stop_wait_abandoned = true;
        ble_faulted = true;
        ++ble_diagnostics.lifecycle_errors;
        portEXIT_CRITICAL(&state_lock);
        ESP_LOGE(TAG,
                 "NimBLE stop worker timed out; restart blocked until reboot");
        return ESP_ERR_TIMEOUT;
    }

    portENTER_CRITICAL(&state_lock);
    const esp_err_t result = stop_task_result;
    portEXIT_CRITICAL(&state_lock);
    return result;
}

esp_err_t RobotBle_Stop(void)
{
    TaskHandle_t host_task;
    TaskHandle_t status_task;
    const TaskHandle_t caller = xTaskGetCurrentTaskHandle();

    portENTER_CRITICAL(&state_lock);
    host_task = host_task_handle;
    status_task = status_task_handle;
    portEXIT_CRITICAL(&state_lock);
    if ((caller == host_task) || (caller == status_task))
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (!BeginLifecycleOperation())
    {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t result = StopLocked();
    EndLifecycleOperation();
    return result;
}

esp_err_t RobotBle_Start(void)
{
    esp_err_t result;
    gpio_config_t button_config = {
        .pin_bit_mask = (1ULL << ROBOT_BLE_PAIR_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    int rc;
    EventBits_t start_bits;
    bool custom_objects_initialized = false;

    if (!BeginLifecycleOperation())
    {
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL(&state_lock);
    const bool already_started = ble_started || ble_starting ||
                                 host_stopping || ble_faulted;
    if (!already_started)
    {
        ble_starting = true;
        ble_started = false;
        host_ready = false;
        host_synced = false;
        host_stopping = false;
        status_stop_requested = false;
        lifecycle_cleanup_failed = false;
        stop_wait_abandoned = false;
        stop_task_handle = NULL;
        status_task_handle = NULL;
        advertising_retry_pending = false;
        advertising_failure_count = 0U;
    }
    portEXIT_CRITICAL(&state_lock);
    if (already_started)
    {
        EndLifecycleOperation();
        return ESP_ERR_INVALID_STATE;
    }

    lifecycle_events = xEventGroupCreateStatic(&lifecycle_event_storage);
    (void)xEventGroupClearBits(lifecycle_events, BLE_EVENT_ALL);
    portENTER_CRITICAL(&state_lock);
    memset(&ble_diagnostics, 0, sizeof(ble_diagnostics));
    portEXIT_CRITICAL(&state_lock);
    result = nvs_flash_init();
    if (result != ESP_OK)
    {
        /* Never erase NVS implicitly; pairing and future OTA state live here. */
        goto start_failed_without_host;
    }

    result = nimble_port_init();
    if (result != ESP_OK)
    {
        goto start_failed_without_host;
    }
    portENTER_CRITICAL(&state_lock);
    nimble_initialized = true;
    portEXIT_CRITICAL(&state_lock);

    result = gpio_config(&button_config);
    if (result != ESP_OK)
    {
        goto start_failed_without_host;
    }

    ble_hs_cfg.sync_cb = OnSync;
    ble_hs_cfg.reset_cb = OnReset;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1U;
    ble_hs_cfg.sm_mitm = 0U;
    ble_hs_cfg.sm_sc = 1U;
    /*
     * The board has no input/output capability, so Android uses LE Secure
     * Connections Just Works.  NimBLE's SC-only mode upgrades every secured
     * GATT attribute to security level 4 (authenticated/MITM), which Just
     * Works cannot satisfy.  Legacy pairing remains compiled out, therefore
     * disabling this policy does not re-enable legacy pairing; it allows the
     * encrypted, bonded SC link to access READ_ENC / WRITE_ENC attributes.
     */
    ble_hs_cfg.sm_sc_only = 0U;
    ble_hs_cfg.sm_our_key_dist =
        BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist =
        BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    rc = ble_gatts_count_cfg(services);
    if (rc == 0)
    {
        rc = ble_gatts_add_svcs(services);
    }
    if (rc == 0)
    {
        rc = ble_svc_gap_device_name_set(ROBOT_BLE_DEVICE_NAME);
    }
    if (rc != 0)
    {
        result = ESP_FAIL;
        goto start_failed_without_host;
    }
    ble_store_config_init();
    ble_npl_event_init(&pairing_reset_event, PairingResetHostEvent, NULL);
    pairing_reset_event_initialized = true;
    rc = ble_npl_callout_init(&advertising_retry_callout,
                              nimble_port_get_dflt_eventq(),
                              AdvertisingRetryEvent, NULL);
    if (rc != BLE_NPL_OK)
    {
        result = ESP_FAIL;
        goto start_failed_without_host;
    }
    advertising_retry_callout_initialized = true;
    custom_objects_initialized = true;

    if (xTaskCreatePinnedToCore(HostTask, "nimble_host",
                                NIMBLE_HS_STACK_SIZE, NULL,
                                configMAX_PRIORITIES - 4,
                                &host_task_handle,
                                NIMBLE_CORE) != pdPASS)
    {
        result = ESP_ERR_NO_MEM;
        goto start_failed_without_host;
    }

    start_bits = xEventGroupWaitBits(
        lifecycle_events,
        BLE_EVENT_START_OK | BLE_EVENT_START_FAIL | BLE_EVENT_HOST_EXIT,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(ROBOT_BLE_START_TIMEOUT_MS));
    if (((start_bits & BLE_EVENT_START_OK) == 0U) ||
        ((start_bits & BLE_EVENT_HOST_EXIT) != 0U) || !IsHostReady())
    {
        result = ((start_bits & BLE_EVENT_START_FAIL) != 0U) ?
            ESP_FAIL : ESP_ERR_TIMEOUT;
        if (StopLocked() != ESP_OK)
        {
            portENTER_CRITICAL(&state_lock);
            ble_faulted = true;
            portEXIT_CRITICAL(&state_lock);
        }
        EndLifecycleOperation();
        return result;
    }

    if (xTaskCreatePinnedToCore(StatusTask, "robot_ble_status",
                                ROBOT_BLE_STATUS_TASK_STACK, NULL,
                                ROBOT_BLE_STATUS_TASK_PRIORITY,
                                &status_task_handle,
                                NIMBLE_CORE) != pdPASS)
    {
        result = ESP_ERR_NO_MEM;
        (void)StopLocked();
        EndLifecycleOperation();
        return result;
    }

    portENTER_CRITICAL(&state_lock);
    const bool start_still_valid = host_task_handle != NULL && host_ready &&
                                   host_synced && !host_stopping &&
                                   !lifecycle_cleanup_failed;
    if (start_still_valid)
    {
        ble_started = true;
        ble_starting = false;
    }
    portEXIT_CRITICAL(&state_lock);
    if (!start_still_valid)
    {
        (void)StopLocked();
        EndLifecycleOperation();
        return ESP_FAIL;
    }
    ESP_LOGI(TAG,
             "NimBLE ready and advertising; non-zero motion disabled");
    EndLifecycleOperation();
    return ESP_OK;

start_failed_without_host:
    if (custom_objects_initialized || advertising_retry_callout_initialized)
    {
        if (advertising_retry_callout_initialized)
        {
            ble_npl_callout_deinit(&advertising_retry_callout);
            advertising_retry_callout_initialized = false;
        }
    }
    if (pairing_reset_event_initialized)
    {
        ble_npl_event_deinit(&pairing_reset_event);
        pairing_reset_event_initialized = false;
    }
    portENTER_CRITICAL(&state_lock);
    const bool deinit_nimble = nimble_initialized;
    portEXIT_CRITICAL(&state_lock);
    esp_err_t deinit_result = ESP_OK;
    if (deinit_nimble)
    {
        deinit_result = nimble_port_deinit();
    }
    portENTER_CRITICAL(&state_lock);
    if (deinit_result == ESP_OK)
    {
        nimble_initialized = false;
    }
    else
    {
        ble_faulted = true;
        ++ble_diagnostics.lifecycle_errors;
    }
    ble_starting = false;
    ble_started = false;
    host_task_handle = NULL;
    status_task_handle = NULL;
    portEXIT_CRITICAL(&state_lock);
    EndLifecycleOperation();
    return result;
}

bool RobotBle_IsReady(void)
{
    bool ready;

    portENTER_CRITICAL(&state_lock);
    ready = ble_started && host_ready && host_synced && !host_stopping;
    portEXIT_CRITICAL(&state_lock);
    return ready;
}

bool RobotBle_IsConnected(void)
{
    return GetConnectionSnapshot(NULL, NULL, NULL);
}

void RobotBle_GetDiagnostics(RobotBleDiagnostics_t *output)
{
    if (output == NULL)
    {
        return;
    }

    portENTER_CRITICAL(&state_lock);
    *output = ble_diagnostics;
    portEXIT_CRITICAL(&state_lock);
}
