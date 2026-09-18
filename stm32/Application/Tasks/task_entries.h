#ifndef TASK_ENTRIES_H
#define TASK_ENTRIES_H

void SensorTask_Entry(void *argument);
void MotorTask_Entry(void *argument);
void BatteryTask_Entry(void *argument);
void CanTask_Entry(void *argument);
void CommunicationTask_Entry(void *argument);
void DisplayTask_Entry(void *argument);
void StorageTask_Entry(void *argument);

#endif
