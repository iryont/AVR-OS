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

#include <string.h>
#include "scheduler.h"
#include "mutex.h"
#include "memory.h"

#if OS_CFG_DYNAMIC
// tasks which are gone but their memory is not, the idle task takes care of it
static OsTask *osDeadList = NULL;

// the idle task calls osTaskReap this way, once there is a reason to. Called
// directly it would make the heap a part of every application, including
// the ones which do not use it: the linker drops only what nobody refers to.
void (*osTaskReaper)(void) = NULL;
#endif

static uint8_t osTaskPriority(uint8_t priority)
{
    if(priority < OS_PRIORITY_MIN)
        return OS_PRIORITY_MIN;

    if(priority > OS_PRIORITY_MAX)
        return OS_PRIORITY_MAX;

    return priority;
}

static OsTask *osTaskSetup(void (*function)(void*), void *param, void *memory, uint16_t size, uint8_t priority, uint8_t dynamic)
{
    if(!memory || size < OS_TASK_MEMORY(OS_STACK_MIN))
        return NULL;

    // task control block is placed on top of the stack, which grows away from it
    OsTask *task = (OsTask*)((uint8_t*)memory + size - sizeof(OsTask));
    memset(task, 0, sizeof(OsTask));

#if OS_CFG_STACK_CHECK
    memset(memory, OS_STACK_FILL, size - sizeof(OsTask));
#endif

    // stack pointer and stack chunk
    task->sp = osPortStackInit((uint8_t*)task - 1, function, param);
#if OS_CFG_STACK_CHECK || OS_CFG_DYNAMIC
    task->stack = memory;
#endif

#if OS_CFG_DYNAMIC
    task->dynamic = dynamic;
#else
    (void)dynamic;
#endif

    task->priority = osTaskPriority(priority);
    task->basePriority = task->priority;

    // run it right away if it is more important than we are
    OS_CRITICAL {
        osReady(task);
        osSchedule(0);
    }

    return task;
}

OsTask *osTaskCreate(void (*function)(void*), void *param, void *memory, uint16_t size, uint8_t priority)
{
    return osTaskSetup(function, param, memory, size, priority, 0);
}

static void osTaskDetach(OsTask *task)
{
    if((task->state & OS_TASK_STATE) == OS_TASK_READY)
        osListRemove(&osReadyList, task);
    else
        osUnblock(task);
}

void osTaskDelete(OsTask *task)
{
    OS_CRITICAL {
        if(!task)
            task = osCurrentTask;

        // the idle task is here to stay
        if(task != &osIdleTask && (task->state & OS_TASK_STATE) != OS_TASK_DEAD) {
            // remove this task from the lists
            osTaskDetach(task);
            task->state = OS_TASK_DEAD;

#if OS_CFG_MUTEX_INHERITANCE
            // nobody is going to unlock them anymore
            osMutexReleaseAll(task);
#endif

#if OS_CFG_DYNAMIC
            // memory cannot be released here, we might be still using the stack
            if(task->dynamic) {
                task->next = osDeadList;
                osDeadList = task;
            }
#endif

            // there is no way back once the current task is dead
            if(task == osCurrentTask)
                osDispatch();

            // somebody more important than we are might have got a mutex
            osSchedule(0);
        }
    }
}

void osTaskExit(void)
{
    osTaskDelete(NULL);

    // should never get here, unless it is the idle task which cannot be deleted
    while(1);
}

void osTaskSuspend(OsTask *task)
{
    OS_CRITICAL {
        if(!task)
            task = osCurrentTask;

        uint8_t state = task->state & OS_TASK_STATE;
        if(task != &osIdleTask && state != OS_TASK_SUSPENDED && state != OS_TASK_DEAD) {
            osTaskDetach(task);

            // whatever the task was waiting for, it did not get it
            task->result = false;
            task->state = OS_TASK_SUSPENDED;

            if(task == osCurrentTask)
                osDispatch();
        }
    }
}

void osTaskResumeFromISR(OsTask *task)
{
    if((task->state & OS_TASK_STATE) == OS_TASK_SUSPENDED)
        osReady(task);
}

void osTaskResume(OsTask *task)
{
    OS_CRITICAL {
        osTaskResumeFromISR(task);
        osSchedule(0);
    }
}

void osTaskSetPriority(OsTask *task, uint8_t priority)
{
    priority = osTaskPriority(priority);

    OS_CRITICAL {
        if(!task)
            task = osCurrentTask;

        // priority inherited from a mutex stays as long as the mutex does
        task->basePriority = priority;
        priority = osInheritedPriority(task);

        if(priority > task->priority) {
            osPriorityRaise(task, priority);
        } else if(priority < task->priority) {
            task->priority = priority;
            osRequeue(task);
        }

        osSchedule(0);
    }
}

uint8_t osTaskGetPriority(OsTask *task)
{
    if(!task)
        task = osCurrentTask;

    return task->basePriority;
}

uint8_t osTaskGetState(OsTask *task)
{
    if(!task)
        task = osCurrentTask;

    return task->state & OS_TASK_STATE;
}

OsTask *osTaskCurrent(void)
{
    return osCurrentTask;
}

#if OS_CFG_STACK_CHECK
// number of bytes of the stack which have never been used
uint16_t osTaskStackFree(OsTask *task)
{
    if(!task)
        task = osCurrentTask;

    uint8_t *stack = task->stack;
    while(stack < (uint8_t*)RAMEND && *stack == OS_STACK_FILL)
        stack++;

    return stack - task->stack;
}

void osStackOverflowHook(OsTask *task) __attribute__ ((weak));
void osStackOverflowHook(OsTask *task)
{
    (void)task;

    // memory is corrupted, there is nothing to come back to
    cli();
    while(1);
}
#endif

#if OS_CFG_DYNAMIC
OsTask *osTaskCreateDynamic(void (*function)(void*), void *param, uint16_t stackSize, uint8_t priority)
{
    uint16_t size = OS_TASK_MEMORY(stackSize);

    // do not count on the idle task, it may not get a chance to run for a long time
    osTaskReaper = osTaskReap;
    osTaskReap();

    void *memory = osMalloc(size);
    if(!memory)
        return NULL;

    OsTask *task = osTaskSetup(function, param, memory, size, priority, 1);
    if(!task)
        osFree(memory);

    return task;
}

// free the memory used by tasks which are gone. A task cannot do it on its
// own since it needs its stack until the very end, but anybody else can:
// once a task is on the list it is not running and it never will.
void osTaskReap(void)
{
    while(1) {
        OsTask *task;

        OS_CRITICAL {
            task = osDeadList;
            if(task)
                osDeadList = task->next;
        }

        if(!task)
            break;

        osFree(task->stack);
    }
}
#endif
