#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

#define SCH_MAX_TASKS       12U
#define SCH_TICK_MS         10U
#define SCH_NO_TASK_ID      0U

typedef void (*SCH_TaskFunction)(void);

void SCH_Init(void);
void SCH_Update(void);
void SCH_Dispatch_Tasks(void);

uint32_t SCH_Add_Task(SCH_TaskFunction pFunction,
                      uint32_t DELAY,
                      uint32_t PERIOD);

uint8_t SCH_Delete_Task(uint32_t taskID);
uint32_t SCH_Get_Time_ms(void);

#endif
