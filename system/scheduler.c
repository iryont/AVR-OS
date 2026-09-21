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

#include "scheduler.h"
#include "mutex.h"
#include "timer.h"

// current task control block
OsTask *osCurrentTask = NULL;

// tasks which are ready to run, the most important one first
// the current task is not on the list
OsTask *osReadyList = NULL;

// tasks which wait with a timeout, the one which expires first at the front
static OsTask *osTimerList = NULL;

static OsTick osTicks = OS_CFG_TICK_START;

#if OS_CFG_AGING
static uint8_t osAging = OS_CFG_AGING_TICKS;
#endif

// lists are ordered by priority, the task is placed behind
// the last one which priority is not lower than the key
void osListInsert(OsTask **list, OsTask *task, uint8_t key)
{
    OsTask *it;
    while((it = *list) != NULL && it->priority >= key)
        list = &it->next;

    task->next = it;
    *list = task;
}

void osListRemove(OsTask **list, OsTask *task)
{
    OsTask *it;
    while((it = *list) != NULL) {
        if(it == task) {
            *list = task->next;
            return;
        }

        list = &it->next;
    }
}

static void osTimerListInsert(OsTask *task, OsTick timeout)
{
    // compare the time left rather than the time itself, tick counter overflows
    OsTask **list = &osTimerList;
    OsTask *it;
    while((it = *list) != NULL && (OsTick)(it->wake - osTicks) <= timeout)
        list = &it->timerNext;

    task->wake = osTicks + timeout;
    task->timerNext = it;
    task->state |= OS_TASK_TIMED;
    *list = task;
}

static void osTimerListRemove(OsTask *task)
{
    OsTask **list = &osTimerList;
    OsTask *it;
    while((it = *list) != NULL) {
        if(it == task) {
            *list = task->timerNext;
            return;
        }

        list = &it->timerNext;
    }
}

// priority of the task has changed, so did its place on the list
void osRequeue(OsTask *task)
{
    OsTask **list = task->waitList;
    if((task->state & OS_TASK_STATE) == OS_TASK_READY)
        list = &osReadyList;

    if(list) {
        osListRemove(list, task);
        osListInsert(list, task, task->priority);
    }
}

// priority of the task without the part gained by aging
uint8_t osInheritedPriority(OsTask *task)
{
    uint8_t priority = task->basePriority;

#if OS_CFG_MUTEX_INHERITANCE
    // the most important task waiting for a mutex is the first one on its list
    for(OsMutex *mutex = task->mutexes; mutex; mutex = mutex->nextOwned) {
        OsTask *waiter = mutex->waiters;
        if(waiter && waiter->priority > priority)
            priority = waiter->priority;
    }
#endif

    return priority;
}

void osPriorityRaise(OsTask *task, uint8_t priority)
{
    while(task->priority < priority) {
        task->priority = priority;
        osRequeue(task);

#if OS_CFG_MUTEX_INHERITANCE
        // the task waits for a mutex itself, so its owner has to hurry up as well
        if(task->state & OS_TASK_MUTEX) {
            task = ((OsMutex*)task->waitList)->owner;
            continue;
        }
#endif

        break;
    }
}

// priority gained by aging lasts until the task gives the processor away
static inline void osSettle(OsTask *task) __attribute__ ((always_inline));
static inline void osSettle(OsTask *task)
{
#if OS_CFG_AGING
    if(task->priority != task->basePriority)
        task->priority = osInheritedPriority(task);
#else
    (void)task;
#endif
}

// switches to the first task on the ready list. Returns once the current
// task is running again, with the result it has been woken up with.
uint8_t osDispatch(void)
{
#if OS_CFG_STACK_CHECK
    // comes first, so there is nothing to be preserved while the hook is called
    if(*osCurrentTask->stack != OS_STACK_FILL)
        osStackOverflowHook(osCurrentTask);
#endif

    // never empty, the idle task is either running or ready
    OsTask *prev = osCurrentTask;
    OsTask *next = osReadyList;

    osReadyList = next->next;
    next->state = OS_TASK_RUNNING;
    osCurrentTask = next;

    return osPortSwitch(next, prev);
}

// is there anybody to give the processor to?
static inline uint8_t osScheduleNeeded(uint8_t yield) __attribute__ ((always_inline));
static inline uint8_t osScheduleNeeded(uint8_t yield)
{
    OsTask *head = osReadyList;
    return head && (uint8_t)(head->priority + yield) > osCurrentTask->priority;
}

// gives the processor to the first task on the ready list if it is more
// important than the current one (yield = 0) or at least as important as
// the current one (yield = 1), in which case they take turns.
void osSchedule(uint8_t yield)
{
    if(osScheduleNeeded(yield)) {
        OsTask *task = osCurrentTask;

        osSettle(task);

        // preempted task stays ahead of the tasks with the same priority
        // a task which yields goes behind them
        task->state = OS_TASK_READY;
        osListInsert(&osReadyList, task, task->priority + 1 - yield);

        osDispatch();
    }
}

// Blocks the current task until somebody wakes it up or the timeout expires,
// flags set in the state of the task beforehand are preserved.
uint8_t osBlock(OsTask **list, OsTick timeout)
{
    OsTask *task = osCurrentTask;

    // the idle task never waits, there would be nobody left to run. Whoever
    // tries to wait on its behalf gets a timeout right away: main() before
    // the system is running, the idle hook, interrupt handlers and timers
    // which happen to be called while the idle task is the current one.
    if(task == &osIdleTask) {
        task->state = OS_TASK_RUNNING;
        return false;
    }

    osSettle(task);

    task->state |= OS_TASK_BLOCKED;
    task->waitList = list;

    if(list)
        osListInsert(list, task, task->priority);

    if(timeout != OS_WAIT_FOREVER)
        osTimerListInsert(task, timeout);

    // switch to another task, we are back once somebody wakes us up
    return osDispatch();
}

void osUnblock(OsTask *task)
{
    if(task->waitList) {
        osListRemove(task->waitList, task);
        task->waitList = NULL;
    }

    if(task->state & OS_TASK_TIMED)
        osTimerListRemove(task);
}

void osReady(OsTask *task)
{
    task->state = OS_TASK_READY;
    osListInsert(&osReadyList, task, task->priority);
}

void osWake(OsTask *task, uint8_t result)
{
    task->result = result;

    osUnblock(task);
    osReady(task);
}

void osTickHandler(void)
{
    OsTask *task;
    OsTick ticks = ++osTicks;

    // time is up
    while((task = osTimerList) != NULL && task->wake == ticks)
        osWake(task, false);

#if OS_CFG_TIMERS
    if(osTimers && osTimers->when == ticks)
        osTimerTick(ticks);
#endif

#if OS_CFG_AGING
    // priority gained by aging is good for a single tick. Without that, tasks
    // would have to outgrow each other over and over again to get their turn.
    osSettle(osCurrentTask);

    // tasks which have no chance to run as long as the current one does are
    // getting more important. Tasks as important as the current one are left
    // alone, they take turns with it anyway (unless round robin is disabled).
    // order of the list is preserved: nobody gets ahead of the current task.
    if(--osAging == 0) {
        osAging = OS_CFG_AGING_TICKS;

        uint8_t priority = osCurrentTask->priority;
        for(task = osReadyList; task; task = task->next) {
            if(task->priority && task->priority < OS_CFG_AGING_LIMIT && task->priority + OS_CFG_ROUND_ROBIN <= priority)
                task->priority++;
        }
    }
#endif

    // most of the time there is nobody to switch to, which is not worth a call
    if(osScheduleNeeded(OS_CFG_ROUND_ROBIN))
        osSchedule(OS_CFG_ROUND_ROBIN);
}

OsTick osTickCount(void)
{
    OsTick ticks;

    OS_CRITICAL {
        ticks = osTicks;
    }

    return ticks;
}

void osYield(void)
{
    OS_CRITICAL {
        osSchedule(1);
    }
}

void osDelay(OsTick ticks)
{
    OS_CRITICAL {
        if(ticks)
            osBlock(NULL, ticks);
        else
            osSchedule(1);
    }
}

// Delay for periodic tasks: the period is counted from the previous wake-up,
// not from now, so time taken by the task itself does not add up.
void osDelayUntil(OsTick *previous, OsTick period)
{
    OS_CRITICAL {
        OsTick wake = *previous + period;
        OsTick left = wake - osTicks;

        *previous = wake;

        // do not wait at all if we are late already
        if((OsTick)(left - 1) < period)
            osBlock(NULL, left);
    }
}
