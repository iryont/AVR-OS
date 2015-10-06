/*
 * Operating system for Atmel AVR microcontrollers
 * Copyright (c) 2015 Konrad Kusnierz <iryont@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdlib.h>
#include "scheduler.h"
#include "asm.h"

// current task control block
TaskControlBlock* volatile osCurrentTask = NULL;

void osIdleTask(void *parameters)
{
    while(1);
}

void osSchedulerInit()
{
    osCurrentTask = osCreateTask(osIdleTask, NULL, 64, 0);
}

void osWait(uint16_t wait)
{
    DISABLE_INTERRUPTS
    osCurrentTask->wait = wait;
    ENABLE_INTERRUPTS

    // yield and switch to another process
    osNonResumableYield();
}

void osTaskExit()
{
    DISABLE_INTERRUPTS

    // remove this task from the list
    osTasksQueueRemove(osCurrentTask);

    // free the memory used by this task
    osTaskDestroy(osCurrentTask);

    // no current task
    osCurrentTask = NULL;

    // yield without saving the context
    // any context switch will set SREG register
    // all interrupts will be re-enabled
    osNonSavableYield();
}

void osContextSwitch(int8_t resumable, int8_t incremental)
{
    uint8_t queueSize = osTasksQueueSize();

    TaskControlBlock *target = NULL;
    for(uint8_t i = 0; i < queueSize; i++) {
        TaskControlBlock *task = osTasksQueueAt(i);

        // task requested to wait for a while
        if(incremental) {
            if(task != osCurrentTask)
                task->age++;

            if(task->wait > 0) {
                task->wait--;
                continue;
            }
        }

        // no task yet, so choose this one
        if(!target) {
            target = task;
            continue;
        }

        // apparently task doesn't want to be resumed right away
        // but if it's the only task then it has no choice
        if(!resumable && task == osCurrentTask && queueSize != 1)
            continue;

        if(task->priority * queueSize + task->age > target->priority * queueSize + target->age)
            target = task;
    }

    osCurrentTask = target;
    osCurrentTask->age = 0;
}

TaskControlBlock* osCreateTask(void (*taskFunction)(void*), void *taskParameter, uint8_t taskStackSize, uint8_t taskPriority)
{
    TaskControlBlock *task = (TaskControlBlock*)malloc(sizeof(TaskControlBlock));

    task->topOfStack = osInitializeStack(malloc(taskStackSize) + taskStackSize - 1, taskFunction, taskParameter);
    task->priority = taskPriority;
    task->wait = 0;
    task->age = 0;

    osTasksQueueInsert(task);
    return task;
}

void osTaskDestroy(TaskControlBlock *task)
{
    free(task->topOfStack);
    free(task);
}
