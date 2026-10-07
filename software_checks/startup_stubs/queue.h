#pragma once
typedef void *QueueHandle_t;
int xQueueOverwrite(QueueHandle_t queue, const void *item);
