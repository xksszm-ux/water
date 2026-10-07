#ifndef MOTOR_STEP4_TEST_H
#define MOTOR_STEP4_TEST_H

/*
 * Keep disabled until the power rails, common ground and TB6612 STBY have
 * passed the multimeter checklist and the wheels are raised off the bench.
 */
#ifndef MOTOR_STEP4_TEST_ENABLED
#define MOTOR_STEP4_TEST_ENABLED 0
#endif

void MotorStep4Test_Process(void);

#endif
