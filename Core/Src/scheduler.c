#include "scheduler.h"

typedef struct
{
    SCH_TaskFunction pTask;
    uint32_t delay;       /* Delta delay relative to previous node */
    uint32_t period;
    uint32_t taskID;
    int16_t next;
    uint8_t active;
    uint8_t queued;
} SCH_Task;

static SCH_Task tasks[SCH_MAX_TASKS];
static int16_t queueHead = -1;
static uint32_t nextTaskID = 1U;
static volatile uint32_t schedulerTicks = 0U;

static uint32_t irq_lock(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void irq_unlock(uint32_t primask)
{
    __set_PRIMASK(primask);
}

/* Insert an already allocated task into the delta-time queue.
   This search is done by Add/Dispatch, never by SCH_Update(). */
static void insert_existing_task(uint16_t index, uint32_t delay)
{
    int16_t previous = -1;
    int16_t current = queueHead;
    uint32_t remaining = delay;

    while (current >= 0 && remaining >= tasks[current].delay)
    {
        remaining -= tasks[current].delay;
        previous = current;
        current = tasks[current].next;
    }

    tasks[index].delay = remaining;
    tasks[index].next = current;
    tasks[index].queued = 1U;

    if (current >= 0)
    {
        tasks[current].delay -= remaining;
    }

    if (previous < 0)
    {
        queueHead = (int16_t)index;
    }
    else
    {
        tasks[previous].next = (int16_t)index;
    }
}

static void remove_from_queue(uint16_t index)
{
    int16_t previous = -1;
    int16_t current = queueHead;

    while (current >= 0 && current != (int16_t)index)
    {
        previous = current;
        current = tasks[current].next;
    }

    if (current < 0)
    {
        tasks[index].queued = 0U;
        return;
    }

    int16_t next = tasks[current].next;

    /* Preserve the absolute due time of the following task. */
    if (next >= 0)
    {
        tasks[next].delay += tasks[current].delay;
    }

    if (previous < 0)
    {
        queueHead = next;
    }
    else
    {
        tasks[previous].next = next;
    }

    tasks[index].next = -1;
    tasks[index].queued = 0U;
}

void SCH_Init(void)
{
    uint32_t primask = irq_lock();

    for (uint16_t i = 0; i < SCH_MAX_TASKS; i++)
    {
        tasks[i].pTask = 0;
        tasks[i].delay = 0U;
        tasks[i].period = 0U;
        tasks[i].taskID = SCH_NO_TASK_ID;
        tasks[i].next = -1;
        tasks[i].active = 0U;
        tasks[i].queued = 0U;
    }

    queueHead = -1;
    nextTaskID = 1U;
    schedulerTicks = 0U;

    irq_unlock(primask);
}

/* O(1): only the head delta is decremented.
   No task-array search and no callback execution in the ISR. */
void SCH_Update(void)
{
    schedulerTicks++;

    if (queueHead >= 0 && tasks[queueHead].delay > 0U)
    {
        tasks[queueHead].delay--;
    }
}

uint32_t SCH_Add_Task(SCH_TaskFunction pFunction,
                      uint32_t DELAY,
                      uint32_t PERIOD)
{
    if (pFunction == 0)
    {
        return SCH_NO_TASK_ID;
    }

    uint32_t primask = irq_lock();
    int16_t freeIndex = -1;

    for (uint16_t i = 0; i < SCH_MAX_TASKS; i++)
    {
        if (tasks[i].active == 0U)
        {
            freeIndex = (int16_t)i;
            break;
        }
    }

    if (freeIndex < 0)
    {
        irq_unlock(primask);
        return SCH_NO_TASK_ID;
    }

    uint32_t id = nextTaskID++;
    if (id == SCH_NO_TASK_ID)
    {
        id = nextTaskID++;
    }

    tasks[freeIndex].pTask = pFunction;
    tasks[freeIndex].period = PERIOD;
    tasks[freeIndex].taskID = id;
    tasks[freeIndex].active = 1U;
    tasks[freeIndex].queued = 0U;
    tasks[freeIndex].next = -1;

    insert_existing_task((uint16_t)freeIndex, DELAY);

    irq_unlock(primask);
    return id;
}

/* Return 0 on success, 1 if the ID does not exist. */
uint8_t SCH_Delete_Task(uint32_t taskID)
{
    if (taskID == SCH_NO_TASK_ID)
    {
        return 1U;
    }

    uint32_t primask = irq_lock();

    for (uint16_t i = 0; i < SCH_MAX_TASKS; i++)
    {
        if (tasks[i].active && tasks[i].taskID == taskID)
        {
            if (tasks[i].queued)
            {
                remove_from_queue(i);
            }

            tasks[i].active = 0U;
            tasks[i].pTask = 0;
            tasks[i].period = 0U;
            tasks[i].taskID = SCH_NO_TASK_ID;
            irq_unlock(primask);
            return 0U;
        }
    }

    irq_unlock(primask);
    return 1U;
}

void SCH_Dispatch_Tasks(void)
{
    while (1)
    {
        uint32_t primask = irq_lock();

        if (queueHead < 0 || tasks[queueHead].delay != 0U)
        {
            irq_unlock(primask);
            break;
        }

        uint16_t index = (uint16_t)queueHead;
        queueHead = tasks[index].next;
        tasks[index].next = -1;
        tasks[index].queued = 0U;

        SCH_TaskFunction function = tasks[index].pTask;
        uint32_t period = tasks[index].period;
        uint32_t id = tasks[index].taskID;

        irq_unlock(primask);

        if (function != 0)
        {
            function();
        }

        /* The callback may have deleted itself or another task. */
        primask = irq_lock();

        if (tasks[index].active &&
            tasks[index].taskID == id &&
            period > 0U)
        {
            insert_existing_task(index, period);
        }
        else if (tasks[index].active &&
                 tasks[index].taskID == id &&
                 period == 0U)
        {
            tasks[index].active = 0U;
            tasks[index].pTask = 0;
            tasks[index].taskID = SCH_NO_TASK_ID;
        }

        irq_unlock(primask);
    }
}

uint32_t SCH_Get_Time_ms(void)
{
    uint32_t ticks;
    uint32_t primask = irq_lock();
    ticks = schedulerTicks;
    irq_unlock(primask);

    return ticks * SCH_TICK_MS;
}


