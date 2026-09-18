#include "robot_uart_link.h"

#include <inttypes.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define ROBOT_UART_PORT                     UART_NUM_2
#define ROBOT_UART_RX_RING_SIZE             1024
#define ROBOT_UART_EVENT_QUEUE_LENGTH       16
#define ROBOT_UART_ACK_QUEUE_LENGTH         4
#define ROBOT_UART_RX_SCRATCH_SIZE          128
#define ROBOT_UART_RX_TASK_STACK_BYTES      4096
#define ROBOT_UART_LINK_TASK_STACK_BYTES    4096
#define ROBOT_UART_RX_TASK_PRIORITY         12
#define ROBOT_UART_LINK_TASK_PRIORITY       11
#define ROBOT_UART_TX_TIMEOUT_MS            20U
#define ROBOT_UART_STOP_RETRY_MS            250U
#define ROBOT_UART_HEARTBEAT_PERIOD_MS      500U
#define ROBOT_UART_STATUS_REQUEST_MS        1000U
#define ROBOT_UART_RESPONSE_TIMEOUT_MS      1200U
#define ROBOT_UART_STATUS_FRESH_MS          500U
#define ROBOT_UART_LOG_PERIOD_MS            2000U

#define LINK_NOTIFY_RX_FAULT                (1UL << 0)
#define LINK_NOTIFY_STOP_REQUEST            (1UL << 1)

typedef struct
{
    RobotProtocolStatus_t status;
    uint32_t received_at_ms;
} RobotStatusEnvelope_t;

static const char *TAG = "robot_uart";

static QueueHandle_t uart_event_queue;
static QueueHandle_t ack_queue;
static QueueHandle_t status_mailbox;
static TaskHandle_t rx_task_handle;
static TaskHandle_t link_task_handle;
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static RobotUartLinkDiagnostics_t diagnostics;
static bool driver_started;
static bool link_online;

static uint32_t NowMs(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void SetLinkOnline(bool online)
{
    portENTER_CRITICAL(&state_lock);
    link_online = online;
    portEXIT_CRITICAL(&state_lock);
}

static void IncrementCounter(uint32_t *counter)
{
    portENTER_CRITICAL(&state_lock);
    ++*counter;
    portEXIT_CRITICAL(&state_lock);
}

static void NotifyLinkFault(void)
{
    TaskHandle_t task;

    portENTER_CRITICAL(&state_lock);
    task = link_task_handle;
    portEXIT_CRITICAL(&state_lock);

    if (task != NULL)
    {
        (void)xTaskNotify(task, LINK_NOTIFY_RX_FAULT, eSetBits);
    }
}

static void ResetReceiveStream(RobotProtocolParser_t *parser,
                               bool flush_driver)
{
    RobotProtocolParser_Reset(parser);
    if (flush_driver)
    {
        (void)uart_flush_input(ROBOT_UART_PORT);
        if (uart_event_queue != NULL)
        {
            (void)xQueueReset(uart_event_queue);
        }
    }
}

static void PublishFrame(const RobotProtocolFrame_t *frame, uint32_t now_ms)
{
    RobotProtocolAck_t ack;
    RobotStatusEnvelope_t envelope;

    if (RobotProtocol_DecodeAck(frame, &ack))
    {
        if (xQueueSend(ack_queue, &ack, 0) != pdPASS)
        {
            RobotProtocolAck_t discarded;
            (void)xQueueReceive(ack_queue, &discarded, 0);
            (void)xQueueSend(ack_queue, &ack, 0);
        }
        IncrementCounter(&diagnostics.received_frames);
        return;
    }

    if (RobotProtocol_DecodeStatus(frame, &envelope.status))
    {
        envelope.received_at_ms = now_ms;
        (void)xQueueOverwrite(status_mailbox, &envelope);
        IncrementCounter(&diagnostics.received_frames);
        return;
    }

    IncrementCounter(&diagnostics.semantic_rejects);
}

static void DrainParser(RobotProtocolParser_t *parser, uint32_t now_ms)
{
    RobotProtocolFrame_t frame;

    for (;;)
    {
        const RobotProtocolParseResult_t result =
            RobotProtocolParser_Next(parser, now_ms, &frame);

        if (result == ROBOT_PROTOCOL_PARSE_NONE)
        {
            return;
        }
        if (result == ROBOT_PROTOCOL_PARSE_FRAME)
        {
            PublishFrame(&frame, now_ms);
        }
        else if (result == ROBOT_PROTOCOL_PARSE_DROPPED_BAD_CRC)
        {
            IncrementCounter(&diagnostics.crc_errors);
        }
        else if (result == ROBOT_PROTOCOL_PARSE_DROPPED_TIMEOUT)
        {
            IncrementCounter(&diagnostics.parser_timeouts);
        }
        else
        {
            IncrementCounter(&diagnostics.semantic_rejects);
        }
    }
}

static bool ProcessDataEvent(RobotProtocolParser_t *parser, size_t byte_count)
{
    uint8_t scratch[ROBOT_UART_RX_SCRATCH_SIZE];
    size_t remaining = byte_count;

    /* Expire an old partial frame before adding bytes from a new UART event. */
    DrainParser(parser, NowMs());

    while (remaining > 0U)
    {
        const size_t requested = remaining < sizeof(scratch) ?
            remaining : sizeof(scratch);
        const int received = uart_read_bytes(ROBOT_UART_PORT,
                                             scratch,
                                             requested,
                                             pdMS_TO_TICKS(20));
        if (received <= 0)
        {
            return false;
        }

        const uint32_t now_ms = NowMs();
        for (int index = 0; index < received; ++index)
        {
            if (!RobotProtocolParser_PushByte(parser, scratch[index], now_ms))
            {
                RobotProtocolParser_Reset(parser);
                IncrementCounter(&diagnostics.receive_overflows);
                NotifyLinkFault();
                if (!RobotProtocolParser_PushByte(parser,
                                                  scratch[index],
                                                  now_ms))
                {
                    return false;
                }
            }
            DrainParser(parser, now_ms);
        }

        remaining -= (size_t)received;
    }

    return true;
}

static void RobotUartRxTask(void *argument)
{
    (void)argument;
    RobotProtocolParser_t parser;

    /* Start only after both UART tasks have been created successfully. */
    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    RobotProtocolParser_Init(&parser);

    for (;;)
    {
        uart_event_t event;
        if (xQueueReceive(uart_event_queue, &event,
                          pdMS_TO_TICKS(20)) != pdTRUE)
        {
            DrainParser(&parser, NowMs());
            continue;
        }

        switch (event.type)
        {
        case UART_DATA:
            if (!ProcessDataEvent(&parser, event.size))
            {
                IncrementCounter(&diagnostics.uart_errors);
                ResetReceiveStream(&parser, true);
                NotifyLinkFault();
            }
            break;

        case UART_FIFO_OVF:
        case UART_BUFFER_FULL:
            IncrementCounter(&diagnostics.receive_overflows);
            ResetReceiveStream(&parser, true);
            NotifyLinkFault();
            break;

        case UART_BREAK:
        case UART_FRAME_ERR:
        case UART_PARITY_ERR:
            IncrementCounter(&diagnostics.uart_errors);
            ResetReceiveStream(&parser, true);
            NotifyLinkFault();
            break;

        default:
            break;
        }
    }
}

static bool SendFrame(const uint8_t *frame, size_t length)
{
    if ((frame == NULL) || (length == 0U) ||
        (length > ROBOT_PROTOCOL_MAX_FRAME_SIZE))
    {
        return false;
    }

    const int written = uart_write_bytes(ROBOT_UART_PORT,
                                         (const char *)frame,
                                         length);
    if ((written != (int)length) ||
        (uart_wait_tx_done(ROBOT_UART_PORT,
                           pdMS_TO_TICKS(ROBOT_UART_TX_TIMEOUT_MS)) != ESP_OK))
    {
        IncrementCounter(&diagnostics.transmit_errors);
        return false;
    }

    IncrementCounter(&diagnostics.transmitted_frames);
    return true;
}

static void EnterSafeHandshake(uint16_t *control_sequence,
                               bool *stop_acknowledged,
                               uint32_t *stop_acknowledged_at_ms,
                               uint32_t *next_stop_ms,
                               uint32_t now_ms,
                               bool count_link_loss)
{
    SetLinkOnline(false);
    *stop_acknowledged = false;
    *stop_acknowledged_at_ms = 0U;
    ++*control_sequence;
    /* A real deadline is wrap-safe; zero stops being "due" after 2^31 ms. */
    *next_stop_ms = now_ms;
    (void)xQueueReset(ack_queue);
    /* A pre-fault STATUS must never make a recovered link look fresh. */
    (void)xQueueReset(status_mailbox);
    if (count_link_loss)
    {
        IncrementCounter(&diagnostics.link_loss_count);
    }
}

static void RobotUartLinkTask(void *argument)
{
    (void)argument;
    uint8_t frame[ROBOT_PROTOCOL_MAX_FRAME_SIZE];
    uint16_t control_sequence = 0U;
    uint16_t auxiliary_sequence = 0U;
    uint16_t pending_stop_sequence = 0U;
    uint32_t next_stop_ms = 0U;
    uint32_t next_heartbeat_ms = 0U;
    uint32_t next_status_request_ms = 0U;
    uint32_t last_response_ms = 0U;
    uint32_t last_log_ms = 0U;
    uint32_t stop_acknowledged_at_ms = 0U;
    bool stop_acknowledged = false;

    /* Start only after both UART tasks have been created successfully. */
    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    portENTER_CRITICAL(&state_lock);
    link_task_handle = xTaskGetCurrentTaskHandle();
    portEXIT_CRITICAL(&state_lock);

    EnterSafeHandshake(&control_sequence, &stop_acknowledged,
                       &stop_acknowledged_at_ms,
                       &next_stop_ms, NowMs(), false);
    pending_stop_sequence = control_sequence;

    for (;;)
    {
        uint32_t notifications = 0U;
        (void)xTaskNotifyWait(0U, UINT32_MAX, &notifications,
                              pdMS_TO_TICKS(20));
        const uint32_t now_ms = NowMs();

        if ((notifications & LINK_NOTIFY_RX_FAULT) != 0U)
        {
            EnterSafeHandshake(&control_sequence, &stop_acknowledged,
                               &stop_acknowledged_at_ms,
                               &next_stop_ms, now_ms, true);
            pending_stop_sequence = control_sequence;
        }

        if ((notifications & LINK_NOTIFY_STOP_REQUEST) != 0U)
        {
            EnterSafeHandshake(&control_sequence, &stop_acknowledged,
                               &stop_acknowledged_at_ms,
                               &next_stop_ms, now_ms, false);
            pending_stop_sequence = control_sequence;
        }

        RobotProtocolAck_t ack;
        while (xQueueReceive(ack_queue, &ack, 0) == pdTRUE)
        {
            last_response_ms = now_ms;
            if (!stop_acknowledged &&
                (ack.sequence == pending_stop_sequence) &&
                (ack.request_type == ROBOT_PROTOCOL_TYPE_STOP) &&
                (ack.result == ROBOT_PROTOCOL_ACK_OK))
            {
                stop_acknowledged = true;
                stop_acknowledged_at_ms = now_ms;
                next_heartbeat_ms = now_ms;
                next_status_request_ms = now_ms;
                ESP_LOGI(TAG, "Safe STOP handshake acknowledged, seq=%u",
                         (unsigned int)ack.sequence);
            }
        }

        RobotStatusEnvelope_t status_envelope;
        const bool have_status =
            xQueuePeek(status_mailbox, &status_envelope, 0) == pdTRUE;
        if (have_status &&
            ((uint32_t)(now_ms - status_envelope.received_at_ms) <=
             ROBOT_UART_STATUS_FRESH_MS))
        {
            /* The freshness test above already handles tick wrap safely. */
            last_response_ms = status_envelope.received_at_ms;
            if (stop_acknowledged &&
                ((int32_t)(status_envelope.received_at_ms -
                           stop_acknowledged_at_ms) >= 0) &&
                (status_envelope.status.applied_left_pwm == 0) &&
                (status_envelope.status.applied_right_pwm == 0))
            {
                SetLinkOnline(true);
            }
            else if (RobotUartLink_IsOnline())
            {
                ESP_LOGW(TAG,
                         "STM32 status is not a post-STOP zero-output state");
                EnterSafeHandshake(&control_sequence, &stop_acknowledged,
                                   &stop_acknowledged_at_ms,
                                   &next_stop_ms, now_ms, true);
                pending_stop_sequence = control_sequence;
            }
            else
            {
                SetLinkOnline(false);
            }
        }
        else if (RobotUartLink_IsOnline())
        {
            ESP_LOGW(TAG, "STM32 status became stale; restarting handshake");
            EnterSafeHandshake(&control_sequence, &stop_acknowledged,
                               &stop_acknowledged_at_ms,
                               &next_stop_ms, now_ms, true);
            pending_stop_sequence = control_sequence;
        }
        else
        {
            SetLinkOnline(false);
        }

        if (stop_acknowledged &&
            ((uint32_t)(now_ms - last_response_ms) >
             ROBOT_UART_RESPONSE_TIMEOUT_MS))
        {
            ESP_LOGW(TAG, "STM32 response timeout; returning to STOP handshake");
            EnterSafeHandshake(&control_sequence, &stop_acknowledged,
                               &stop_acknowledged_at_ms,
                               &next_stop_ms, now_ms, true);
            pending_stop_sequence = control_sequence;
        }

        /* Keep retransmitting STOP until a post-ACK zero-output STATUS exists. */
        if (!RobotUartLink_IsOnline())
        {
            if ((int32_t)(now_ms - next_stop_ms) >= 0)
            {
                const size_t length = RobotProtocol_EncodeStop(
                    pending_stop_sequence, true, frame, sizeof(frame));
                if (length == 0U)
                {
                    IncrementCounter(&diagnostics.transmit_errors);
                }
                else
                {
                    (void)SendFrame(frame, length);
                }
                IncrementCounter(&diagnostics.stop_retries);
                next_stop_ms = now_ms + ROBOT_UART_STOP_RETRY_MS;
            }

            if ((uint32_t)(now_ms - last_log_ms) >= ROBOT_UART_LOG_PERIOD_MS)
            {
                ESP_LOGW(TAG, "STM32 link offline; sending safe STOP retries");
                last_log_ms = now_ms;
            }
            continue;
        }

        if ((int32_t)(now_ms - next_heartbeat_ms) >= 0)
        {
            const size_t length = RobotProtocol_EncodeHeartbeat(
                auxiliary_sequence++, false, frame, sizeof(frame));
            if ((length == 0U) || !SendFrame(frame, length))
            {
                EnterSafeHandshake(&control_sequence, &stop_acknowledged,
                                   &stop_acknowledged_at_ms,
                                   &next_stop_ms, now_ms, true);
                pending_stop_sequence = control_sequence;
                continue;
            }
            next_heartbeat_ms = now_ms + ROBOT_UART_HEARTBEAT_PERIOD_MS;
        }

        if ((int32_t)(now_ms - next_status_request_ms) >= 0)
        {
            const size_t length = RobotProtocol_EncodeStatusRequest(
                auxiliary_sequence++, false, frame, sizeof(frame));
            if ((length == 0U) || !SendFrame(frame, length))
            {
                EnterSafeHandshake(&control_sequence, &stop_acknowledged,
                                   &stop_acknowledged_at_ms,
                                   &next_stop_ms, now_ms, true);
                pending_stop_sequence = control_sequence;
                continue;
            }
            next_status_request_ms = now_ms + ROBOT_UART_STATUS_REQUEST_MS;
        }
    }
}

static void DeleteResources(void)
{
    if (rx_task_handle != NULL)
    {
        vTaskDelete(rx_task_handle);
        rx_task_handle = NULL;
    }
    if (link_task_handle != NULL)
    {
        vTaskDelete(link_task_handle);
        link_task_handle = NULL;
    }
    if (driver_started)
    {
        (void)uart_driver_delete(ROBOT_UART_PORT);
        driver_started = false;
    }
    if (ack_queue != NULL)
    {
        vQueueDelete(ack_queue);
        ack_queue = NULL;
    }
    if (status_mailbox != NULL)
    {
        vQueueDelete(status_mailbox);
        status_mailbox = NULL;
    }
    uart_event_queue = NULL;
}

esp_err_t RobotUartLink_Start(void)
{
    const uart_config_t uart_config = {
        .baud_rate = ROBOT_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = { .allow_pd = 0 }
    };

    portENTER_CRITICAL(&state_lock);
    const bool already_started = driver_started;
    portEXIT_CRITICAL(&state_lock);
    if (already_started)
    {
        return ESP_ERR_INVALID_STATE;
    }

    memset(&diagnostics, 0, sizeof(diagnostics));
    SetLinkOnline(false);

    ack_queue = xQueueCreate(ROBOT_UART_ACK_QUEUE_LENGTH,
                             sizeof(RobotProtocolAck_t));
    status_mailbox = xQueueCreate(1, sizeof(RobotStatusEnvelope_t));
    if ((ack_queue == NULL) || (status_mailbox == NULL))
    {
        DeleteResources();
        return ESP_ERR_NO_MEM;
    }

    esp_err_t result = uart_driver_install(
        ROBOT_UART_PORT, ROBOT_UART_RX_RING_SIZE, 0,
        ROBOT_UART_EVENT_QUEUE_LENGTH, &uart_event_queue, 0);
    if (result != ESP_OK)
    {
        DeleteResources();
        return result;
    }
    driver_started = true;

    result = uart_param_config(ROBOT_UART_PORT, &uart_config);
    if (result == ESP_OK)
    {
        result = uart_set_pin(ROBOT_UART_PORT,
                              ROBOT_UART_TX_GPIO,
                              ROBOT_UART_RX_GPIO,
                              UART_PIN_NO_CHANGE,
                              UART_PIN_NO_CHANGE);
    }
    if (result == ESP_OK)
    {
        /* UART idle is high; keep an unplugged STM32 RX path deterministic. */
        result = gpio_set_pull_mode((gpio_num_t)ROBOT_UART_RX_GPIO,
                                    GPIO_PULLUP_ONLY);
    }
    if (result == ESP_OK)
    {
        result = uart_flush_input(ROBOT_UART_PORT);
    }
    if (result != ESP_OK)
    {
        DeleteResources();
        return result;
    }

    const BaseType_t application_core = xPortGetCoreID();
    if (xTaskCreatePinnedToCore(RobotUartRxTask, "robot_uart_rx",
                                ROBOT_UART_RX_TASK_STACK_BYTES, NULL,
                                ROBOT_UART_RX_TASK_PRIORITY,
                                &rx_task_handle,
                                application_core) != pdPASS)
    {
        DeleteResources();
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreatePinnedToCore(RobotUartLinkTask, "robot_uart_link",
                                ROBOT_UART_LINK_TASK_STACK_BYTES, NULL,
                                ROBOT_UART_LINK_TASK_PRIORITY,
                                &link_task_handle,
                                application_core) != pdPASS)
    {
        DeleteResources();
        return ESP_ERR_NO_MEM;
    }

    /* Release both tasks only after initialization is fully committed. */
    xTaskNotifyGive(rx_task_handle);
    xTaskNotifyGive(link_task_handle);

    ESP_LOGI(TAG, "UART2 started: TX=GPIO%d RX=GPIO%d %d 8N1",
             ROBOT_UART_TX_GPIO, ROBOT_UART_RX_GPIO,
             ROBOT_UART_BAUD_RATE);
    return ESP_OK;
}

bool RobotUartLink_IsOnline(void)
{
    bool online;
    portENTER_CRITICAL(&state_lock);
    online = link_online;
    portEXIT_CRITICAL(&state_lock);
    return online;
}

esp_err_t RobotUartLink_RequestStop(void)
{
    TaskHandle_t task;

    portENTER_CRITICAL(&state_lock);
    task = link_task_handle;
    portEXIT_CRITICAL(&state_lock);

    if (task == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    return (xTaskNotify(task, LINK_NOTIFY_STOP_REQUEST, eSetBits) == pdPASS) ?
        ESP_OK : ESP_FAIL;
}

bool RobotUartLink_GetStatus(RobotProtocolStatus_t *status,
                             uint32_t *age_ms)
{
    RobotStatusEnvelope_t envelope;

    if ((status == NULL) || (status_mailbox == NULL) ||
        (xQueuePeek(status_mailbox, &envelope, 0) != pdTRUE))
    {
        return false;
    }

    const uint32_t age = NowMs() - envelope.received_at_ms;
    if (age_ms != NULL)
    {
        *age_ms = age;
    }
    if ((age > ROBOT_UART_STATUS_FRESH_MS) || !RobotUartLink_IsOnline())
    {
        return false;
    }

    *status = envelope.status;
    return true;
}

void RobotUartLink_GetDiagnostics(RobotUartLinkDiagnostics_t *output)
{
    if (output == NULL)
    {
        return;
    }

    portENTER_CRITICAL(&state_lock);
    *output = diagnostics;
    portEXIT_CRITICAL(&state_lock);
}
