#include "protocol.h"
#include "robot_protocol.h"
#include "robot_ble_protocol.h"
#include "can_protocol.h"
#include <string.h>
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
void ControlChecks_SetNow(uint32_t time);
int WireChecks_Run(void)
{
    uint8_t wire[PROTOCOL_MAX_FRAME_SIZE];
    /* Expired shared state must encode as invalid on CAN as well as UART. */
    RobotState_Init();
    SensorMessage_t sample={.valid_mask=SENSOR_VALID_DISTANCE,.timestamp_ms=0};
    RobotState_UpdateSensor(&sample);
    RobotState_UpdateBattery(4500,1,false,0);
    ControlChecks_SetNow(1501);
    RobotState_InvalidateSensorIfStale();
    RobotState_InvalidateBatteryIfStale();
    RobotStatus_t expired;
    RobotState_GetSnapshot(&expired);
    CanProtocol_EncodeStatus(&expired,wire);
    CHECK(wire[0]==255 && wire[1]==255 &&
          (wire[7]&(ROBOT_ERROR_SENSOR|ROBOT_ERROR_BATTERY_ADC))==
          (ROBOT_ERROR_SENSOR|ROBOT_ERROR_BATTERY_ADC));
    RobotDiagnostics_t source={.free_heap=0x1234,.minimum_heap=0x0102,
        .stack_free={1,2,3,4,5,6,65535},.health_fault_mask=0x41,
        .receive_errors=0x12345678,.response_drops=0xffffffff,.sensor_queue_drops=17};
    CHECK(Protocol_EncodeDiagnostics(65535,&source,wire,sizeof(wire))==42);
    CHECK(wire[3]==0x82 && wire[7]==32 && wire[8]==2);
    CHECK(wire[9]==0x34 && wire[10]==0x12 && wire[28]==0x78 && wire[31]==0x12);
    RobotProtocolParser_t parser; RobotProtocolFrame_t frame;
    RobotProtocolParser_Init(&parser);
    for (unsigned i=0;i<42;++i) CHECK(RobotProtocolParser_PushByte(&parser,wire[i],i));
    CHECK(RobotProtocolParser_Next(&parser,42,&frame)==ROBOT_PROTOCOL_PARSE_FRAME);
    RobotDiagnostics_t decoded;
    CHECK(RobotProtocol_DecodeDiagnostics(&frame,&decoded));
    CHECK(decoded.free_heap==source.free_heap && decoded.receive_errors==source.receive_errors);
    CHECK(decoded.stack_free[6]==65535 && decoded.response_drops==0xffffffff);
    CHECK(decoded.health_fault_mask==0x41);
    frame.flags=1; CHECK(!RobotProtocol_DecodeDiagnostics(&frame,&decoded)); frame.flags=0;
    frame.payload[0]=1; CHECK(!RobotProtocol_DecodeDiagnostics(&frame,&decoded)); frame.payload[0]=2;
    frame.payload_length=31; CHECK(!RobotProtocol_DecodeDiagnostics(&frame,&decoded)); frame.payload_length=32;
    frame.payload[19]=0x80; CHECK(!RobotProtocol_DecodeDiagnostics(&frame,&decoded));
    CHECK(Protocol_EncodeDiagnostics(1,&source,wire,41)==0);

    const uint8_t vector[16]={1,3,255,7,0x78,0x56,0x34,0x12,
        255,255,255,255,5,0,0,0};
    RobotCanFeedback_t report={.flags=3,.sequence=255,.result=7,
        .rejected=0x12345678,.replays=UINT32_MAX,.age_ms=5}, received, current;
    size_t feedback_n=Protocol_EncodeCanFeedback(9,&report,wire,sizeof(wire));
    CHECK(feedback_n==26 && memcmp(wire+8,vector,16)==0);
    RobotProtocolParser_Init(&parser);
    for(size_t i=0;i<feedback_n;++i) CHECK(RobotProtocolParser_PushByte(&parser,wire[i],0));
    CHECK(RobotProtocolParser_Next(&parser,0,&frame)==ROBOT_PROTOCOL_PARSE_FRAME);
    CHECK(RobotProtocol_DecodeCanFeedback(&frame,&received) && received.rejected==0x12345678);
    frame.flags=1; CHECK(!RobotProtocol_DecodeCanFeedback(&frame,&received)); frame.flags=0;
    frame.payload[0]=2; CHECK(!RobotProtocol_DecodeCanFeedback(&frame,&received)); frame.payload[0]=1;
    frame.payload[1]=2; CHECK(!RobotProtocol_DecodeCanFeedback(&frame,&received)); frame.payload[1]=3;
    frame.payload[3]=10; CHECK(!RobotProtocol_DecodeCanFeedback(&frame,&received)); frame.payload[3]=7;
    frame.payload_length=15; CHECK(!RobotProtocol_DecodeCanFeedback(&frame,&received)); frame.payload_length=16;
    CHECK(RobotCanFeedback_Current(&received,true,true,UINT32_MAX-10,2,&current) && current.age_ms==18);
    CHECK(current.flags==7);
    CHECK(RobotCanFeedback_Current(&received,true,true,0,3000,&current));
    CHECK(!RobotCanFeedback_Current(&received,true,true,0,3001,&current));
    CHECK(!RobotCanFeedback_Current(&received,true,false,0,0,&current));
    CHECK(!RobotCanFeedback_Current(&received,false,true,0,0,&current));
    received.age_ms=UINT32_MAX-1;
    CHECK(RobotCanFeedback_Current(&received,true,true,0,5,&current) && current.age_ms==UINT32_MAX);
    uint8_t feedback_ble[ROBOT_BLE_CAN_FEEDBACK_SIZE];
    report.flags |= ROBOT_CAN_FEEDBACK_AVAILABLE;
    CHECK(RobotBleProtocol_EncodeCanFeedback(&report,feedback_ble,sizeof(feedback_ble))==18);
    CHECK(feedback_ble[0]==1 && feedback_ble[1]==7 && memcmp(feedback_ble+2,vector+2,14)==0);
    CHECK(RobotProtocol_Crc16CcittFalse(feedback_ble,16)==
        ((uint16_t)feedback_ble[16] | ((uint16_t)feedback_ble[17]<<8)));
    CHECK(RobotBleProtocol_EncodeCanFeedback(&report,feedback_ble,17)==0);
    CHECK(Protocol_EncodeCanFeedback(1,&report,wire,sizeof(wire))==0); /* BLE-only availability */
    report.flags=3; CHECK(Protocol_EncodeCanFeedback(1,&report,wire,25)==0);
    report=(RobotCanFeedback_t){.age_ms=UINT32_MAX};
    CHECK(RobotBleProtocol_EncodeCanFeedback(&report,feedback_ble,sizeof(feedback_ble))==18);
    CHECK(feedback_ble[1]==0 && feedback_ble[12]==255);
    CHECK(RobotCanFeedback_Current(&report,true,true,0,1,&current) && current.flags==4);
    CHECK(RobotBleProtocol_EncodeCanFeedback(&current,feedback_ble,18)==18 && feedback_ble[1]==4);
    report.flags=8; CHECK(RobotBleProtocol_EncodeCanFeedback(&report,feedback_ble,18)==0);

    /* Full status path: STM32 -> UART -> ESP decoder -> BLE payload. */
    ProtocolStatus_t status={.battery_mv=4500,.distance_mm=123,.valid_flags=3,.owner=255};
    size_t n=Protocol_EncodeStatus(1,&status,wire,sizeof(wire)); CHECK(n==22);
    RobotProtocolParser_Init(&parser);
    for (size_t i=0;i<n;++i) CHECK(RobotProtocolParser_PushByte(&parser,wire[i],100));
    CHECK(RobotProtocolParser_Next(&parser,100,&frame)==ROBOT_PROTOCOL_PARSE_FRAME);
    RobotProtocolStatus_t remote; CHECK(RobotProtocol_DecodeStatus(&frame,&remote));
    RobotBleStatusFields_t ble={.battery_mv=remote.battery_mv,.distance_mm=remote.distance_mm,
        .valid_flags=remote.valid_flags,.owner=remote.owner};
    uint8_t ble_wire[20]; CHECK(RobotBleProtocol_EncodeStatus(9,&ble,ble_wire,sizeof(ble_wire))==20);
    CHECK(ble_wire[12]==123 && ble_wire[13]==0);

    /* STM32 recovery from noise + bad CRC + concatenated valid frames. */
    n=RobotProtocol_EncodeStop(9,true,wire,sizeof(wire)); CHECK(n==10);
    ProtocolParser_t stm; ProtocolFrame_t stm_frame;
    ProtocolParser_Init(&stm);
    CHECK(ProtocolParser_PushByte(&stm,0x33,0));
    for(size_t i=0;i<n;++i) CHECK(ProtocolParser_PushByte(&stm,wire[i]^(i==n-1?1:0),0));
    for(size_t i=0;i<n;++i) CHECK(ProtocolParser_PushByte(&stm,wire[i],0));
    CHECK(ProtocolParser_Next(&stm,0,&stm_frame)==PROTOCOL_PARSE_DROPPED_BAD_CRC);
    CHECK(ProtocolParser_Next(&stm,0,&stm_frame)==PROTOCOL_PARSE_FRAME && stm_frame.sequence==9);
    ProtocolParser_Init(&stm);
    for(size_t i=0;i<4;++i) CHECK(ProtocolParser_PushByte(&stm,wire[i],UINT32_MAX-20));
    CHECK(ProtocolParser_Next(&stm,40,&stm_frame)==PROTOCOL_PARSE_DROPPED_TIMEOUT);
    for(size_t i=0;i<n;++i) CHECK(ProtocolParser_PushByte(&stm,wire[i],41));
    CHECK(ProtocolParser_Next(&stm,41,&stm_frame)==PROTOCOL_PARSE_FRAME);
    ProtocolParser_Init(&stm); wire[7]=33;
    for(size_t i=0;i<8;++i) CHECK(ProtocolParser_PushByte(&stm,wire[i],0));
    CHECK(ProtocolParser_Next(&stm,0,&stm_frame)==PROTOCOL_PARSE_DROPPED_BAD_LENGTH);
    return 0;
}
