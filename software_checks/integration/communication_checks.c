/* Include the production dispatcher to test its private parser/queue boundary.
 * No parser or handler algorithm is copied. UART bytes and OS events are fakes. */
#include "../../stm32/Application/Tasks/communication_task.c"
#include "robot_protocol.h"
#include "robot_link_safety.h"
#include "motor_task.h"
#include "robot_ble_protocol.h"
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static uint8_t incoming[96];
static size_t incoming_count, incoming_pos;
void MotorStep4Test_Process(void) {}
void AppTasks_GetDiagnostics(RtosDiagnostics_t *d) { memset(d,0,sizeof(*d)); d->motor_stack_free_bytes=123; d->health_fault_mask=0x12; }
void UartDriver_GetDiagnostics(UartDriverDiagnostics_t *d) { memset(d,0,sizeof(*d)); d->hardware_errors=2; }
bool UartDriver_ReadByte(uint8_t *b)
{ if (incoming_pos==incoming_count) return false; *b=incoming[incoming_pos++]; return true; }
HAL_StatusTypeDef UartDriver_Init(osThreadId_t t) { (void)t; return HAL_OK; }
HAL_StatusTypeDef UartDriver_Recover(osThreadId_t t) { (void)t; return HAL_OK; }
HAL_StatusTypeDef UartDriver_Service(void) { return HAL_OK; }
uint32_t UartDriver_ConsumeEvents(void) { return 0; }
bool UartDriver_IsTxPending(void) { return false; }
HAL_StatusTypeDef UartDriver_Send(const uint8_t *d, uint16_t n) { (void)d;(void)n;return HAL_OK; }
uint32_t osThreadFlagsClear(uint32_t f) { (void)f; return 0; }
uint32_t osThreadFlagsWait(uint32_t f,uint32_t o,uint32_t t) { (void)f;(void)o;(void)t;return 0; }
osThreadId_t osThreadGetId(void) { return NULL; }
void ControlChecks_SetNow(uint32_t time);
static void Feed(const uint8_t *data, size_t n, uint32_t now, bool *online)
{
    bool requested=false; uint32_t last=0;
    memcpy(incoming,data,n); incoming_count=n; incoming_pos=0;
    ProcessReceivedBytes(now,&requested,online,&last);
}
int CommunicationChecks_Run(void)
{
    uint8_t wire[42]; bool online=false;
    size_t n=RobotProtocol_EncodeStop(1,true,wire,sizeof(wire));
    ProtocolParser_Init(&parser); ResponseQueueReset(); g_uart_parser_timeout_count=0;
    Feed(wire,4,10,&online); CHECK(!online);
    Feed(wire+4,n-4,71,&online);
    CHECK(!online && g_uart_parser_timeout_count==1); /* old partial cannot be revived */
    Feed(wire,n,72,&online); CHECK(online && response_count==1);
    ResponseQueueReset();
    for(unsigned i=0;i<UART_RESPONSE_QUEUE_LENGTH;++i) CHECK(ResponseQueuePush(wire,(uint8_t)n));
    CHECK(!ResponseQueuePush(wire,(uint8_t)n));
    ResponseQueueReset();
    QueueDiagnostics(3); CHECK(response_count==2);
    RobotProtocolParser_t p; RobotProtocolFrame_t f; RobotDiagnostics_t d;
    RobotProtocolParser_Init(&p);
    for(unsigned i=0;i<response_length[0];++i) CHECK(RobotProtocolParser_PushByte(&p,response_queue[0][i],0));
    CHECK(RobotProtocolParser_Next(&p,0,&f)==ROBOT_PROTOCOL_PARSE_FRAME);
    CHECK(RobotProtocol_DecodeDiagnostics(&f,&d) && d.stack_free[1]==123 && d.health_fault_mask==0x12 && d.receive_errors>=2);
    /* Export boundary expires stale state even if Display/Storage never ran. */
    RobotState_Init();
    SensorMessage_t sensor={.timestamp_ms=0,.valid_mask=SENSOR_VALID_DISTANCE,.distance_mm=12};
    RobotState_UpdateSensor(&sensor); RobotState_UpdateBattery(4500,1,false,0);
    ControlChecks_SetNow(1600);
    n=BuildStatusFrame(4); RobotProtocolParser_Init(&p);
    for(size_t i=0;i<n;++i) CHECK(RobotProtocolParser_PushByte(&p,status_buffer[i],1600));
    CHECK(RobotProtocolParser_Next(&p,1600,&f)==ROBOT_PROTOCOL_PARSE_FRAME);
    RobotProtocolStatus_t status; CHECK(RobotProtocol_DecodeStatus(&f,&status));
    CHECK(status.valid_flags==0 && status.battery_mv==65535 && status.distance_mm==65535);
    return 0;
}

void HostControl_Reset(uint32_t time);
int16_t HostControl_LeftOutput(void);
bool HostControl_HasQueuedCommand(void);

/* Drive the real UART dispatcher with ESP32-encoded bytes. */
static void ScenarioFeed(const uint8_t *data, size_t length, uint32_t at_ms,
                         bool *stm32_online, uint32_t *last_frame_ms)
{
    bool status_requested = false;
    ControlChecks_SetNow(at_ms);
    memcpy(incoming, data, length);
    incoming_count = length;
    incoming_pos = 0;
    ProcessReceivedBytes(at_ms, &status_requested, stm32_online, last_frame_ms);
}

static bool DecodeEspFrame(const uint8_t *data, size_t length, uint32_t at_ms,
                           RobotProtocolFrame_t *frame)
{
    RobotProtocolParser_t p;
    RobotProtocolParser_Init(&p);
    for (size_t i = 0; i < length; ++i) {
        if (!RobotProtocolParser_PushByte(&p, data[i], at_ms)) return false;
    }
    return RobotProtocolParser_Next(&p, at_ms, frame) == ROBOT_PROTOCOL_PARSE_FRAME;
}

static bool TakeAck(RobotProtocolAck_t *ack)
{
    uint8_t length;
    const uint8_t *response = ResponseQueueFront(&length);
    RobotProtocolFrame_t frame;
    if (response == NULL || !DecodeEspFrame(response, length, 0, &frame) ||
        !RobotProtocol_DecodeAck(&frame, ack)) return false;
    ResponseQueuePop();
    return true;
}

static bool ReadStatus(uint16_t sequence, uint32_t at_ms, RobotProtocolStatus_t *status)
{
    RobotProtocolFrame_t frame;
    ControlChecks_SetNow(at_ms);
    const uint8_t length = BuildStatusFrame(sequence);
    return length != 0 && DecodeEspFrame(status_buffer, length, at_ms, &frame) &&
           RobotProtocol_DecodeStatus(&frame, status);
}

int CrossMcuScenario_Run(void)
{
    uint8_t wire[ROBOT_PROTOCOL_MAX_FRAME_SIZE];
    RobotLinkSafety_t link;
    RobotProtocolAck_t ack;
    RobotProtocolStatus_t status;
    RobotProtocolFrame_t frame;
    MotorTaskState_t motor;
    uint32_t last_frame_ms = 0;
    bool stm32_online = false;
    uint8_t length;
    const uint8_t *response;
    size_t n;

    HostControl_Reset(100);
    MotorTask_InitState(&motor);
    ProtocolParser_Init(&parser);
    ResponseQueueReset();
    RobotLinkSafety_Start(&link, 10);

    /* Startup: ACK is acceptance; only a later zero-output STATUS proves
       the firmware's software STOP path has run. */
    n = RobotProtocol_EncodeStop(10, true, wire, sizeof(wire));
    CHECK(n == 10);
    ScenarioFeed(wire, n, 100, &stm32_online, &last_frame_ms);
    CHECK(stm32_online && last_frame_ms == 100 && response_count == 1);
    CHECK(TakeAck(&ack) && ack.sequence == 10 && ack.request_type == ROBOT_PROTOCOL_TYPE_STOP &&
          ack.result == ROBOT_PROTOCOL_ACK_OK);
    CHECK(RobotLinkSafety_Acknowledge(&link, &ack, 100));
    CHECK(ReadStatus(1, 100, &status));
    CHECK(!RobotLinkSafety_Update(&link, &status, 100, 100) && !link.online);
    ControlChecks_SetNow(101);
    MotorTask_RunCycle(&motor);
    CHECK(ReadStatus(2, 101, &status) && status.applied_left_pwm == 0);
    CHECK(!RobotLinkSafety_Update(&link, &status, 101, 101) && link.online);

    /* An ACK lost in transit leaves ESP32 offline. A repeated STOP is safe. */
    RobotLinkSafety_Start(&link, 11);
    n = RobotProtocol_EncodeStop(11, true, wire, sizeof(wire));
    ScenarioFeed(wire, n, 200, &stm32_online, &last_frame_ms);
    CHECK(response_count == 1);
    ResponseQueueReset(); /* transport drop, not an ACK policy change */
    CHECK(ReadStatus(3, 201, &status));
    CHECK(!RobotLinkSafety_Update(&link, &status, 201, 201) && !link.online);
    QueueDiagnostics(1);
    response = ResponseQueueFront(&length);
    CHECK(response != NULL && DecodeEspFrame(response, length, 202, &frame));
    RobotDiagnostics_t diagnostics;
    CHECK(RobotProtocol_DecodeDiagnostics(&frame, &diagnostics));
    ResponseQueueReset();
    CHECK(!RobotLinkSafety_Update(&link, NULL, 0, 202) && !link.online);
    ScenarioFeed(wire, n, 450, &stm32_online, &last_frame_ms);
    CHECK(TakeAck(&ack) && ack.result == ROBOT_PROTOCOL_ACK_OK);
    CHECK(RobotLinkSafety_Acknowledge(&link, &ack, 450));
    ControlChecks_SetNow(451);
    MotorTask_RunCycle(&motor);
    CHECK(ReadStatus(4, 451, &status));
    CHECK(!RobotLinkSafety_Update(&link, &status, 451, 451) && link.online);

    /* A corrupt frame cannot become a command or generate an ACK. */
    n = RobotProtocol_EncodeMotor(12, 500, 500, true, wire, sizeof(wire));
    CHECK(n == 15);
    wire[n - 1] ^= 1;
    const uint32_t crc_before = g_uart_crc_error_count;
    ScenarioFeed(wire, n, 500, &stm32_online, &last_frame_ms);
    CHECK(g_uart_crc_error_count == crc_before + 1 && response_count == 0 &&
          !HostControl_HasQueuedCommand());

    /* Saturated response queue loses STOP ACK, but the STOP still executes. */
    for (unsigned i = 0; i < UART_RESPONSE_QUEUE_LENGTH; ++i)
        CHECK(ResponseQueuePush(wire, (uint8_t)n));
    RobotLinkSafety_Start(&link, 12);
    n = RobotProtocol_EncodeStop(12, true, wire, sizeof(wire));
    const uint32_t drops_before = g_uart_response_drop_count;
    ScenarioFeed(wire, n, 600, &stm32_online, &last_frame_ms);
    CHECK(g_uart_response_drop_count == drops_before + 1 && response_count == UART_RESPONSE_QUEUE_LENGTH);
    ControlChecks_SetNow(601);
    MotorTask_RunCycle(&motor);
    CHECK(HostControl_LeftOutput() == 0 && !HostControl_HasQueuedCommand());
    CHECK(ReadStatus(5, 601, &status) && !RobotLinkSafety_Update(&link, &status, 601, 601));
    CHECK(!link.online);
    ResponseQueueReset();
    ScenarioFeed(wire, n, 750, &stm32_online, &last_frame_ms);
    CHECK(TakeAck(&ack) && RobotLinkSafety_Acknowledge(&link, &ack, 750));
    ControlChecks_SetNow(751);
    MotorTask_RunCycle(&motor);
    CHECK(ReadStatus(6, 751, &status));
    CHECK(!RobotLinkSafety_Update(&link, &status, 751, 751) && link.online);

    /* Internal UART MOTOR path exists on STM32. BLE still rejects DRIVE;
       this direct injection proves the present STOP-only ESP policy reacts. */
    n = RobotProtocol_EncodeMotor(13, 400, 300, true, wire, sizeof(wire));
    ScenarioFeed(wire, n, 800, &stm32_online, &last_frame_ms);
    CHECK(TakeAck(&ack) && ack.result == ROBOT_PROTOCOL_ACK_OK);
    ControlChecks_SetNow(801);
    MotorTask_RunCycle(&motor);
    CHECK(HostControl_LeftOutput() == 400 && ReadStatus(7, 801, &status));
    CHECK(status.applied_left_pwm == 400 && status.owner == ROBOT_PROTOCOL_OWNER_UART);
    CHECK(RobotLinkSafety_Update(&link, &status, 801, 801) && !link.online);

    RobotLinkSafety_Start(&link, 14);
    n = RobotProtocol_EncodeStop(14, true, wire, sizeof(wire));
    ScenarioFeed(wire, n, 802, &stm32_online, &last_frame_ms);
    CHECK(TakeAck(&ack) && RobotLinkSafety_Acknowledge(&link, &ack, 802));
    ControlChecks_SetNow(803);
    MotorTask_RunCycle(&motor);
    CHECK(HostControl_LeftOutput() == 0 && !HostControl_HasQueuedCommand());
    CHECK(ReadStatus(8, 803, &status));
    CHECK(!RobotLinkSafety_Update(&link, &status, 803, 803) && link.online);
    n = RobotProtocol_EncodeMotor(13, 400, 300, true, wire, sizeof(wire));
    ScenarioFeed(wire, n, 804, &stm32_online, &last_frame_ms);
    CHECK(TakeAck(&ack) && ack.result == ROBOT_PROTOCOL_ACK_DUPLICATE);
    ControlChecks_SetNow(805);
    MotorTask_RunCycle(&motor);
    CHECK(HostControl_LeftOutput() == 0 && !HostControl_HasQueuedCommand());
    CHECK(RobotLinkSafety_Update(&link, &status, 803, 1304) && !link.online);

    /* Recover from stale STATUS with a new STOP transaction. The previous
       STATUS and ACK cannot qualify the new session. */
    RobotLinkSafety_Start(&link, 15);
    n = RobotProtocol_EncodeStop(15, true, wire, sizeof(wire));
    ScenarioFeed(wire, n, 1400, &stm32_online, &last_frame_ms);
    CHECK(TakeAck(&ack) && RobotLinkSafety_Acknowledge(&link, &ack, 1400));
    CHECK(!RobotLinkSafety_Update(&link, &status, 803, 1400) && !link.online);
    ControlChecks_SetNow(1401);
    MotorTask_RunCycle(&motor);
    CHECK(ReadStatus(9, 1401, &status));
    CHECK(!RobotLinkSafety_Update(&link, &status, 1401, 1401) && link.online);

    /* Heartbeat and diagnostics are communication activity, not a renewal
       of an accepted MOTOR command's 300 ms lease. This MOTOR is injected
       below BLE, whose user-facing DRIVE command is still disabled. */
    ControlChecks_SetNow(1422);
    RobotState_UpdateBattery(4500, 2000, false, 1422);
    n = RobotProtocol_EncodeMotor(16, 300, 200, true, wire, sizeof(wire));
    ScenarioFeed(wire, n, 1422, &stm32_online, &last_frame_ms);
    CHECK(TakeAck(&ack) && ack.result == ROBOT_PROTOCOL_ACK_OK);
    MotorTask_RunCycle(&motor);
    CHECK(HostControl_LeftOutput() == 300);
    n = RobotProtocol_EncodeHeartbeat(17, false, wire, sizeof(wire));
    ScenarioFeed(wire, n, 1700, &stm32_online, &last_frame_ms);
    CHECK(last_frame_ms == 1700 && response_count == 0);
    QueueDiagnostics(2);
    response = ResponseQueueFront(&length);
    CHECK(response != NULL && DecodeEspFrame(response, length, 1722, &frame) &&
          RobotProtocol_DecodeDiagnostics(&frame, &diagnostics));
    ResponseQueueReset();
    ControlChecks_SetNow(1722);
    MotorTask_RunCycle(&motor);
    CHECK(HostControl_LeftOutput() == 300);
    ControlChecks_SetNow(1723);
    MotorTask_RunCycle(&motor);
    CHECK(HostControl_LeftOutput() == 0 && !HostControl_HasQueuedCommand());

    /* A fresh handshake and STATUS across the 32-bit millisecond rollover. */
    HostControl_Reset(UINT32_MAX - 10U);
    MotorTask_InitState(&motor);
    ProtocolParser_Init(&parser);
    ResponseQueueReset();
    stm32_online = false;
    last_frame_ms = 0;
    RobotLinkSafety_Start(&link, UINT16_MAX);
    n = RobotProtocol_EncodeStop(UINT16_MAX, true, wire, sizeof(wire));
    ScenarioFeed(wire, n, UINT32_MAX - 2U, &stm32_online, &last_frame_ms);
    CHECK(TakeAck(&ack) && ack.sequence == UINT16_MAX &&
          RobotLinkSafety_Acknowledge(&link, &ack, UINT32_MAX - 2U));
    ControlChecks_SetNow(2);
    MotorTask_RunCycle(&motor);
    CHECK(ReadStatus(10, 2, &status));
    CHECK(!RobotLinkSafety_Update(&link, &status, 2, 2) && link.online);
    RobotLinkSafety_Start(&link, 0);
    n = RobotProtocol_EncodeStop(0, true, wire, sizeof(wire));
    ScenarioFeed(wire, n, 20, &stm32_online, &last_frame_ms);
    CHECK(TakeAck(&ack) && ack.sequence == 0 &&
          RobotLinkSafety_Acknowledge(&link, &ack, 20));
    ControlChecks_SetNow(21);
    MotorTask_RunCycle(&motor);
    CHECK(ReadStatus(11, 21, &status));
    CHECK(!RobotLinkSafety_Update(&link, &status, 21, 21) && link.online);

    /* CAN/AUTO must agree with the unchanged ESP32 STOP-only policy. */
    HostControl_Reset(100);
    MotorTask_InitState(&motor);
    ProtocolParser_Init(&parser);
    ResponseQueueReset();
    RobotLinkSafety_Start(&link, 10);
    n = RobotProtocol_EncodeStop(10, true, wire, sizeof(wire));
    ScenarioFeed(wire, n, 100, &stm32_online, &last_frame_ms);
    CHECK(TakeAck(&ack) && RobotLinkSafety_Acknowledge(&link, &ack, 100));
    ControlChecks_SetNow(101);
    MotorTask_RunCycle(&motor);
    CHECK(ReadStatus(12, 101, &status));
    CHECK(!RobotLinkSafety_Update(&link, &status, 101, 101) && link.online);
    n = RobotProtocol_EncodeSetMode(11, ROBOT_PROTOCOL_MODE_AUTO, true, wire, sizeof(wire));
    ScenarioFeed(wire, n, 110, &stm32_online, &last_frame_ms);
    CHECK(TakeAck(&ack) && ack.result == ROBOT_PROTOCOL_ACK_OK);
    MotorTask_RunCycle(&motor);
    MotorCommand_t can_motion = {.left_output_permille=400, .right_output_permille=300,
                                 .mode=ROBOT_MODE_AUTO, .enable=true};
    ControlChecks_SetNow(150);
    CHECK(ControlArbiter_SubmitCan(255, &can_motion, 150) == CONTROL_RESULT_SAFETY_BLOCKED);
    CHECK(!HostControl_HasQueuedCommand() && ControlArbiter_GetOwner(150) == CONTROL_SOURCE_NONE);
    MotorTask_RunCycle(&motor);
    CHECK(ReadStatus(13, 150, &status) && status.applied_left_pwm == 0 &&
          status.owner == ROBOT_PROTOCOL_OWNER_NONE && status.mode == ROBOT_PROTOCOL_MODE_AUTO);
    CHECK(!RobotLinkSafety_Update(&link, &status, 150, 150) && link.online);
    ControlChecks_SetNow(151);
    CHECK(ControlArbiter_SubmitCan(0, &can_motion, 151) == CONTROL_RESULT_SAFETY_BLOCKED);
    CHECK(ControlArbiter_SubmitCan(0, &can_motion, 152) == CONTROL_RESULT_REPLAY);
    ControlChecks_SetNow(152);
    MotorTask_RunCycle(&motor);
    CHECK(ReadStatus(14, 152, &status) && status.applied_left_pwm == 0 &&
          !HostControl_HasQueuedCommand());
    CHECK(!RobotLinkSafety_Update(&link, &status, 152, 152) && link.online);
    n = RobotProtocol_EncodeSetMode(12, ROBOT_PROTOCOL_MODE_BLE, true, wire, sizeof(wire));
    ScenarioFeed(wire, n, 160, &stm32_online, &last_frame_ms);
    CHECK(TakeAck(&ack) && ack.result == ROBOT_PROTOCOL_ACK_OK);
    MotorTask_RunCycle(&motor);
    CHECK(ReadStatus(15, 161, &status) && status.mode == ROBOT_PROTOCOL_MODE_BLE &&
          status.applied_left_pwm == 0 && status.owner == ROBOT_PROTOCOL_OWNER_NONE);
    CHECK(!RobotLinkSafety_Update(&link, &status, 161, 161) && link.online);
    n = RobotProtocol_EncodeSetMode(13, ROBOT_PROTOCOL_MODE_AUTO, true, wire, sizeof(wire));
    ScenarioFeed(wire, n, 170, &stm32_online, &last_frame_ms);
    CHECK(TakeAck(&ack) && ack.result == ROBOT_PROTOCOL_ACK_OK);
    MotorTask_RunCycle(&motor);
    ControlChecks_SetNow(191);
    CHECK(ControlArbiter_SubmitCan(1, &can_motion, 191) == CONTROL_RESULT_SAFETY_BLOCKED);
    QueueDiagnostics(16);
    CHECK(response_count==2);
    ResponseQueuePop(); /* existing 0x82 diagnostics; following frame is 0x83 */
    response=ResponseQueueFront(&length);
    CHECK(response && DecodeEspFrame(response,length,192,&frame));
    RobotCanFeedback_t feedback, current;
    CHECK(RobotProtocol_DecodeCanFeedback(&frame,&feedback) && feedback.flags==3 &&
        feedback.sequence==1 && feedback.result==CONTROL_RESULT_SAFETY_BLOCKED &&
        feedback.rejected==3 && feedback.replays==1);
    ResponseQueueReset();
    CHECK(RobotCanFeedback_Current(&feedback,true,link.online,192,193,&current));
    uint8_t ble_feedback[ROBOT_BLE_CAN_FEEDBACK_SIZE];
    CHECK(RobotBleProtocol_EncodeCanFeedback(&current,ble_feedback,sizeof(ble_feedback))==18);
    CHECK(ble_feedback[1]==7 && ble_feedback[2]==1 && ble_feedback[3]==7);
    MotorTask_RunCycle(&motor);
    CHECK(ReadStatus(17,193,&status) && status.applied_left_pwm==0 && status.owner==ROBOT_PROTOCOL_OWNER_NONE);
    CHECK(!RobotLinkSafety_Update(&link,&status,193,193) && link.online);
    RobotLinkSafety_Start(&link,14);
    CHECK(!RobotLinkSafety_Update(&link,NULL,0,194) && !link.online);
    CHECK(!RobotCanFeedback_Current(&feedback,true,link.online,192,194,&current));
    CHECK(!RobotCanFeedback_Current(&feedback,false,true,192,194,&current)); /* cleared on reconnect */
    return 0;
}
