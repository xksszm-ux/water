#include "robot_link_safety.h"
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
int LinkChecks_Run(void)
{
    RobotLinkSafety_t s;
    RobotProtocolStatus_t status={0};
    RobotProtocolAck_t ack={.sequence=7,.request_type=ROBOT_PROTOCOL_TYPE_STOP,.result=ROBOT_PROTOCOL_ACK_OK};
    RobotLinkSafety_Start(&s,7);
    CHECK(!RobotLinkSafety_Update(&s,&status,100,100) && !s.online);
    ack.sequence=6; CHECK(!RobotLinkSafety_Acknowledge(&s,&ack,100));
    ack.sequence=7; ack.request_type=ROBOT_PROTOCOL_TYPE_HEARTBEAT;
    CHECK(!RobotLinkSafety_Acknowledge(&s,&ack,100));
    ack.request_type=ROBOT_PROTOCOL_TYPE_STOP;
    CHECK(RobotLinkSafety_Acknowledge(&s,&ack,100));
    CHECK(!RobotLinkSafety_Update(&s,&status,100,100) && !s.online);
    CHECK(!RobotLinkSafety_Update(&s,&status,101,101) && s.online);
    CHECK(!RobotLinkSafety_Update(&s,&status,0x80000080U,0x80000080U) && s.online);
    CHECK(!RobotLinkSafety_Update(&s,&status,0x80000080U,0x80000274U) && s.online);
    CHECK(RobotLinkSafety_Update(&s,&status,0x80000080U,0x80000275U) && !s.online);
    RobotLinkSafety_Start(&s,8);
    CHECK(!RobotLinkSafety_Acknowledge(&s,&ack,700));
    ack.sequence=8; CHECK(RobotLinkSafety_Acknowledge(&s,&ack,700));
    status.applied_left_pwm=10;
    CHECK(!RobotLinkSafety_Update(&s,&status,701,701) && !s.online);
    CHECK(RobotLinkSafety_Update(&s,&status,1901,1901));
    status.applied_left_pwm=0;
    RobotLinkSafety_Start(&s,65535); ack.sequence=65535;
    CHECK(RobotLinkSafety_Acknowledge(&s,&ack,UINT32_MAX-5));
    CHECK(!RobotLinkSafety_Update(&s,&status,2,2) && s.online);
    status.applied_right_pwm=1;
    CHECK(RobotLinkSafety_Update(&s,&status,3,3) && !s.online);
    RobotLinkSafety_Start(&s,0);
    CHECK(!s.online && !s.acknowledged);
    return 0;
}
