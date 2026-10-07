/* Production control logic; only RTOS/time/motor I/O boundaries replaced. */
#include "app_tasks.h"
#include "control_arbiter.h"
/* Include production code to seed its private counters at wrap, no test API. */
#include "../../stm32/Application/Services/Control/control_arbiter.c"
#include "motor.h"
#include "task_entries.h"
#include "motor_task.h"
#include <string.h>
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static uint32_t now;
static MotorCommand_t queued;
static bool has_command, submit_ok, supply_safe;
static uint8_t stops;
static int scheduler_depth;
static int16_t output_left, output_right;
osMessageQueueId_t g_motor_command_queue;
void vTaskSuspendAll(void) { ++scheduler_depth; }
int xTaskResumeAll(void) { --scheduler_depth; return 0; }
uint32_t osKernelGetTickCount(void) { return now; }
void AppTasks_RequestMotorStop(uint8_t r) { stops |= r; }
uint8_t AppTasks_ConsumeMotorStopRequest(void) { uint8_t r = stops; stops = 0; return r; }
void AppTasks_Heartbeat(AppTaskId_t id) { (void)id; }
uint8_t AppTasks_HealthPoll(void) { return 0U; }
bool AppTasks_SubmitMotorCommandInternal(const MotorCommand_t *command)
{
    if (!submit_ok) return false;
    queued = *command; queued.issued_at_ms = now; has_command = true;
    return true;
}
osStatus_t osMessageQueueGet(osMessageQueueId_t q, void *data, uint8_t *p, uint32_t t)
{
    (void)q; (void)p; (void)t;
    if (!has_command) return -1;
    memcpy(data, &queued, sizeof(queued)); has_command = false; return osOK;
}
HAL_StatusTypeDef MotorDriver_Init(void) { return HAL_OK; }
void MotorDriver_Stop(MotorStopMode_t mode) { (void)mode; output_left = output_right = 0; }
bool MotorDriver_SetOutput(int16_t l, int16_t r) { output_left=l; output_right=r; return true; }
bool PowerSupplyGuard_IsSafe(void) { return supply_safe; }
void Error_Handler(void) { __builtin_trap(); }
osStatus_t osDelayUntil(uint32_t tick) { now = tick; return osOK; }
static void RunMotor(unsigned count)
{
    MotorTaskState_t state;
    MotorTask_InitState(&state);
    for (unsigned i=0; i<count; ++i) {
        MotorTask_RunCycle(&state);
        if (i+1<count) now+=10;
    }
}
static void Reset(uint32_t time)
{
    now=time; stops=0; has_command=false;
    submit_ok=supply_safe=true; scheduler_depth=0;
    RobotState_Init(); ControlArbiter_Init();
    RobotState_UpdateBattery(4500,2000,false,time);
}
int ControlChecks_Run(void)
{
    RobotStatus_t status;
    SensorMessage_t sensor={.timestamp_ms=0,.valid_mask=SENSOR_VALID_DISTANCE};
    Reset(0); RobotState_UpdateSensor(&sensor); now=201; RobotState_InvalidateSensorIfStale();
    RobotState_GetSnapshot(&status); CHECK(status.sensor_valid_mask==0);
    /* A republished cached sample must retain its acquisition deadline. */
    sensor.valid_mask=SENSOR_VALID_MPU6050|SENSOR_VALID_DISTANCE;
    SensorMessage_SetSampleTime(&sensor,199,190,0);
    CHECK(sensor.timestamp_ms==0);
    now=199;
    RobotState_UpdateSensor(&sensor); now=200; RobotState_InvalidateSensorIfStale();
    RobotState_GetSnapshot(&status); CHECK(status.sensor_valid_mask==3);
    CHECK(status.updated_at_ms==199 && status.sensor_updated_at_ms==0);
    now=201; RobotState_InvalidateSensorIfStale();
    RobotState_GetSnapshot(&status); CHECK(status.sensor_valid_mask==0);
    SensorMessage_SetSampleTime(&sensor,10,UINT32_MAX-50,5);
    CHECK(sensor.timestamp_ms==UINT32_MAX-50);
    sensor.valid_mask=SENSOR_VALID_DISTANCE;
    SensorMessage_SetSampleTime(&sensor,10,UINT32_MAX-50,5);
    CHECK(sensor.timestamp_ms==5);
    sensor.valid_mask=SENSOR_VALID_MPU6050;
    SensorMessage_SetSampleTime(&sensor,10,5,UINT32_MAX-50);
    CHECK(sensor.timestamp_ms==5);
    sensor.valid_mask=SENSOR_VALID_NONE;
    SensorMessage_SetSampleTime(&sensor,10,0,0);
    CHECK(sensor.timestamp_ms==10);
    sensor.valid_mask=SENSOR_VALID_DISTANCE;
    sensor.timestamp_ms=UINT32_MAX-50;
    RobotState_UpdateSensor(&sensor); now=151; RobotState_InvalidateSensorIfStale();
    RobotState_GetSnapshot(&status); CHECK(status.sensor_valid_mask==0);
    MotorCommand_t motion={.left_output_permille=400,.right_output_permille=300,
                           .mode=ROBOT_MODE_BLE,.enable=true};
    MotorCommand_t stop={.mode=ROBOT_MODE_BLE};
    Reset(100);
    CHECK(ControlArbiter_SubmitUart(65535,&motion,now)==CONTROL_RESULT_ACCEPTED);
    CHECK(ControlArbiter_SubmitUart(0,&motion,++now)==CONTROL_RESULT_ACCEPTED);
    CHECK(ControlArbiter_SubmitUart(0,&motion,++now)==CONTROL_RESULT_REPLAY);
    CHECK(ControlArbiter_SubmitUart(65534,&stop,now)==CONTROL_RESULT_STOP_ACCEPTED);
    CHECK(stops!=0);
    CHECK(ControlArbiter_SubmitUart(1,&motion,++now)==CONTROL_RESULT_HANDOFF_STOP);
    RunMotor(1); CHECK(output_left==0 && !has_command);
    now+=21;
    CHECK(ControlArbiter_SubmitUart(2,&motion,now)==CONTROL_RESULT_ACCEPTED);
    RunMotor(1); CHECK(output_left==400); CHECK(scheduler_depth==0);
    Reset(0); CHECK(ControlArbiter_SubmitUart(1,&motion,now)==CONTROL_RESULT_ACCEPTED);
    RunMotor(32); CHECK(output_left==0);
    RobotState_GetSnapshot(&status); CHECK(status.error_status & ROBOT_ERROR_COMM_TIMEOUT);
    Reset(UINT32_MAX-100);
    CHECK(ControlArbiter_SubmitUart(1,&motion,now)==CONTROL_RESULT_ACCEPTED);
    RunMotor(32); CHECK(output_left==0);
    Reset(100); RobotState_InvalidateBattery(now);
    CHECK(ControlArbiter_SubmitUart(1,&motion,now)==CONTROL_RESULT_SAFETY_BLOCKED);
    RobotState_UpdateBattery(4500,2000,false,now);
    CHECK(ControlArbiter_SubmitUart(1,&motion,now)==CONTROL_RESULT_REPLAY);
    CHECK(ControlArbiter_SubmitUart(2,&motion,now)==CONTROL_RESULT_ACCEPTED);
    supply_safe=false; RunMotor(1); CHECK(output_left==0);
    supply_safe=true; RobotState_UpdateBattery(4500,2000,false,now);
    RunMotor(1); CHECK(output_left==0);
    now+=1501; CHECK(!RobotState_IsMotorPowerAllowed());
    Reset(100); motion.mode=ROBOT_MODE_AUTO; RobotState_SetMode(ROBOT_MODE_AUTO,now);
    CHECK(ControlArbiter_SubmitCan(255,&motion,now)==CONTROL_RESULT_SAFETY_BLOCKED);
    CHECK(ControlArbiter_SubmitCan(0,&motion,++now)==CONTROL_RESULT_SAFETY_BLOCKED);
    CHECK(ControlArbiter_SubmitCan(0,&motion,++now)==CONTROL_RESULT_REPLAY);
    CHECK(ControlArbiter_GetOwner(now)==CONTROL_SOURCE_NONE && !has_command);
    RunMotor(1); CHECK(output_left==0);
    RobotState_InvalidateBattery(now);
    CHECK(ControlArbiter_SubmitCan(1,&motion,++now)==CONTROL_RESULT_SAFETY_BLOCKED);
    RobotState_UpdateBattery(4500,2000,false,now);
    CHECK(ControlArbiter_SubmitCan(1,&motion,++now)==CONTROL_RESULT_REPLAY);
    CHECK(ControlArbiter_SubmitCan(2,&motion,++now)==CONTROL_RESULT_SAFETY_BLOCKED);
    CHECK(ControlArbiter_SubmitCan(255,&stop,now)==CONTROL_RESULT_STOP_ACCEPTED);
    RunMotor(1); CHECK(output_left==0 && !has_command);
    /* UART plus the internal test source still exercise ownership transfer.
       CAN cannot acquire a motion lease under the default safety policy. */
    Reset(100); motion.mode=ROBOT_MODE_BLE;
    CHECK(ControlArbiter_SubmitUart(1,&motion,now)==CONTROL_RESULT_ACCEPTED);
    CHECK(ControlArbiter_SubmitTest(1,&motion,now)==CONTROL_RESULT_OWNER_BUSY);
    now+=301;
    CHECK(ControlArbiter_SubmitTest(2,&motion,now)==CONTROL_RESULT_HANDOFF_STOP);
    now+=20;
    CHECK(ControlArbiter_SubmitTest(3,&motion,now)==CONTROL_RESULT_ACCEPTED);
    ControlArbiter_ReportSourceFault(CONTROL_SOURCE_TEST,now);
    CHECK(ControlArbiter_GetOwner(now)==CONTROL_SOURCE_NONE && stops!=0);
    now+=21; submit_ok=false;
    CHECK(ControlArbiter_SubmitTest(4,&motion,now)==CONTROL_RESULT_INTERNAL_ERROR);
    CHECK(ControlArbiter_GetOwner(now)==CONTROL_SOURCE_NONE && scheduler_depth==0);
    Reset(100);
    CHECK(ControlArbiter_SubmitUart(1,&motion,now)==CONTROL_RESULT_ACCEPTED);
    /* A CAN STOP still clears queued UART motion, even with an old sequence. */
    CHECK(ControlArbiter_SubmitCan(5,&stop,now)==CONTROL_RESULT_STOP_ACCEPTED);
    CHECK(ControlArbiter_SubmitCan(4,&stop,++now)==CONTROL_RESULT_STOP_ACCEPTED);
    RunMotor(1); CHECK(output_left==0 && !has_command);
    CHECK(ControlArbiter_SubmitUart(2,&motion,now)==CONTROL_RESULT_HANDOFF_STOP);
    now+=21;
    CHECK(ControlArbiter_SubmitUart(2,&motion,now)==CONTROL_RESULT_REPLAY);
    CHECK(ControlArbiter_SubmitUart(3,&motion,now)==CONTROL_RESULT_ACCEPTED);
    RunMotor(1); CHECK(output_left==400);
    Reset(0); motion.mode=ROBOT_MODE_BLE;
    MotorTaskState_t deadline_state;
    MotorTask_InitState(&deadline_state);
    CHECK(ControlArbiter_SubmitUart(1,&motion,now)==CONTROL_RESULT_ACCEPTED);
    MotorTask_RunCycle(&deadline_state); CHECK(output_left==400);
    now=300; MotorTask_RunCycle(&deadline_state); CHECK(output_left==400);
    now=301; MotorTask_RunCycle(&deadline_state); CHECK(output_left==0);
    /* Keep the same task state through reversal, STOP and power recovery. */
    MotorTaskState_t motor;
    Reset(100); motion.mode=ROBOT_MODE_BLE;
    MotorTask_InitState(&motor);
    CHECK(ControlArbiter_SubmitUart(1,&motion,now)==CONTROL_RESULT_ACCEPTED);
    MotorTask_RunCycle(&motor); CHECK(output_left==400);
    motion.left_output_permille=-800; now+=10;
    CHECK(ControlArbiter_SubmitUart(2,&motion,now)==CONTROL_RESULT_ACCEPTED);
    MotorTask_RunCycle(&motor); CHECK(output_left==0);
    now+=10; MotorTask_RunCycle(&motor); CHECK(output_left==-200);
    now+=10; MotorTask_RunCycle(&motor); CHECK(output_left==-800);
    AppTasks_RequestMotorStop(APP_MOTOR_STOP_USER);
    MotorTask_RunCycle(&motor); CHECK(output_left==0 && !motor.command_seen);
    now+=20; MotorTask_RunCycle(&motor); CHECK(output_left==0);
    CHECK(ControlArbiter_SubmitUart(3,&motion,now)==CONTROL_RESULT_ACCEPTED);
    MotorTask_RunCycle(&motor); CHECK(output_left==-800);
    supply_safe=false; MotorTask_RunCycle(&motor); CHECK(output_left==0 && !motor.command_seen);
    supply_safe=true; RobotState_UpdateBattery(4500,2000,false,now);
    MotorTask_RunCycle(&motor); CHECK(output_left==0);
    RobotCanFeedback_t feedback;
    Reset(0);
    ControlArbiter_GetCanFeedback(0,&feedback);
    CHECK(feedback.flags==0 && feedback.age_ms==UINT32_MAX);
    motion.mode=ROBOT_MODE_AUTO;
    CHECK(ControlArbiter_SubmitCan(1,&motion,0)==CONTROL_RESULT_MODE_REJECTED);
    ControlArbiter_GetCanFeedback(1,&feedback);
    CHECK(feedback.flags==3 && feedback.result==CONTROL_RESULT_MODE_REJECTED &&
          feedback.rejected==1 && feedback.age_ms==1);
    RobotState_SetMode(ROBOT_MODE_AUTO,0);
    CHECK(ControlArbiter_SubmitCan(2,&motion,0)==CONTROL_RESULT_SAFETY_BLOCKED);
    CHECK(ControlArbiter_SubmitCan(2,&motion,1)==CONTROL_RESULT_REPLAY);
    ControlArbiter_GetCanFeedback(2,&feedback);
    CHECK(feedback.result==CONTROL_RESULT_REPLAY && feedback.rejected==2 && feedback.replays==1);
    /* Invalid packets affect feedback only, never sequence/owner/STOP. */
    ControlArbiter_RecordInvalidCan(0,false,2);
    ControlArbiter_GetCanFeedback(3,&feedback);
    CHECK(feedback.flags==1 && feedback.result==9 && feedback.sequence==0 &&
          feedback.rejected==3 && feedback.replays==1 && !has_command && stops==0);
    CHECK(ControlArbiter_SubmitCan(3,&stop,3)==CONTROL_RESULT_STOP_ACCEPTED);
    ControlArbiter_GetCanFeedback(4,&feedback);
    CHECK(feedback.result==1 && feedback.rejected==3 && feedback.replays==1);
    can_feedback.rejected=UINT32_MAX; can_feedback.replays=UINT32_MAX;
    ControlArbiter_RecordInvalidCan(255,true,UINT32_MAX-2);
    ControlArbiter_GetCanFeedback(2,&feedback);
    CHECK(feedback.rejected==0 && feedback.age_ms==5 && feedback.flags==3);
    CHECK(ControlArbiter_SubmitCan(3,&motion,4)==CONTROL_RESULT_REPLAY);
    ControlArbiter_GetCanFeedback(4,&feedback); CHECK(feedback.replays==0);
    return 0;
}
void ControlChecks_SetNow(uint32_t time) { now=time; }

/* Shared host boundaries for the cross-MCU scenario. These expose fake I/O,
 * while the arbiter, motor task and RobotState remain production code. */
void HostControl_Reset(uint32_t time) { Reset(time); }
int16_t HostControl_LeftOutput(void) { return output_left; }
bool HostControl_HasQueuedCommand(void) { return has_command; }
