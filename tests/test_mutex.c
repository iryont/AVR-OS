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

// Mutex: exclusion, priority inheritance, timeouts, order of waiting tasks.

// VARIANT: noaging -DOS_CFG_AGING=0
// VARIANT: inversion -DOS_CFG_MUTEX_INHERITANCE=0 -DOS_CFG_AGING=0

#include <string.h>
#include "test.h"

#define TASKS 4

static uint8_t memoryMain[OS_TASK_MEMORY(TEST_STACK)];
static uint8_t memory[TASKS][OS_TASK_MEMORY(TEST_STACK)];
static OsTask *tasks[TASKS];

static OsMutex mutex;
static OsMutex second;

static volatile char order[32];
static volatile uint8_t orders = 0;

static volatile uint16_t shared = 0;

static void mark(char id)
{
    OS_CRITICAL {
        order[orders++] = id;
        order[orders] = 0;
    }
}

// priority of a task, regardless of what it has gained by aging
static uint8_t priority(uint8_t id)
{
    uint8_t priority;

    OS_CRITICAL {
        priority = osInheritedPriority(tasks[id]);
    }

    return priority;
}

#if OS_CFG_AGING
static uint8_t position(char id)
{
    return (uint8_t)(strchr((const char*)order, id) - (const char*)order);
}
#endif

static void expect(const char *expected)
{
    if(strcmp((const char*)order, expected) != 0) {
        testPrint((const char*)order);
        testPrint(expected);
        testFail(0);
    }

    orders = 0;
}

static void spawn(uint8_t id, void (*function)(void*), uint8_t priority)
{
    tasks[id] = osTaskCreate(function, NULL, memory[id], sizeof(memory[id]), priority);
    TEST_ASSERT(tasks[id] != NULL);
}

// waits until all the tasks are gone
static void join(void)
{
    for(uint8_t i = 0; i < TASKS; i++) {
        while(tasks[i] && osTaskGetState(tasks[i]) != OS_TASK_DEAD)
            osDelay(1);

        tasks[i] = NULL;
    }
}

// exclusion

static void worker(void *param)
{
    (void)param;

    for(uint8_t i = 0; i < 100; i++) {
        osMutexLock(&mutex);

        // plenty of time for others to break in
        uint16_t value = shared;
        if(i % 8 == 0)
            osDelay(1);
        else if(i % 2)
            osYield();
        else
            testWork(1);

        shared = value + 1;
        osMutexUnlock(&mutex);
    }
}

// priority inversion

static void inversionLow(void *param)
{
    (void)param;

    osMutexLock(&mutex);
    mark('L');

    // takes long enough for both tasks below to wake up
    testWork(20);

#if OS_CFG_MUTEX_INHERITANCE
    TEST_ASSERT(priority(0) == 3);
#endif

    osMutexUnlock(&mutex);
    TEST_ASSERT(priority(0) == 1);

    mark('l');
}

static void inversionMid(void *param)
{
    (void)param;

    // does not care about the mutex, it just keeps the processor busy
    osDelay(10);
    mark('M');
    testWork(50);
    mark('m');
}

static void inversionHigh(void *param)
{
    (void)param;

    osDelay(5);
    mark('H');
    osMutexLock(&mutex);
    mark('h');
    osMutexUnlock(&mutex);
}

// chain of tasks waiting for each other

#if OS_CFG_MUTEX_INHERITANCE
static void chainLow(void *param)
{
    (void)param;

    osMutexLock(&mutex);
    mark('L');
    testWork(20);
    osMutexUnlock(&mutex);
    mark('l');
}

static void chainMid(void *param)
{
    (void)param;

    osDelay(2);
    osMutexLock(&second);
    mark('M');
    osMutexLock(&mutex);
    mark('m');
    osMutexUnlock(&mutex);
    osMutexUnlock(&second);
}

static void chainHigh(void *param)
{
    (void)param;

    osDelay(4);
    mark('H');
    osMutexLock(&second);
    mark('h');
    osMutexUnlock(&second);
}
#endif

// timeout

static void timeoutLow(void *param)
{
    (void)param;

    osMutexLock(&mutex);
    testWork(30);
    osMutexUnlock(&mutex);
}

static void timeoutHigh(void *param)
{
    (void)param;

    osDelay(2);
    TEST_ASSERT(!osMutexTryLock(&mutex, OS_NO_WAIT));

    OsTick start = osTickCount();
    TEST_ASSERT(!osMutexTryLock(&mutex, 5));
    TEST_ASSERT((OsTick)(osTickCount() - start) == 5);

    // nobody waits for the mutex anymore, so there is no reason to hurry
    TEST_ASSERT(priority(0) == 1);
    TEST_ASSERT(mutex.owner == tasks[0]);

    // this time we are going to make it
    TEST_ASSERT(osMutexTryLock(&mutex, 1000));
    TEST_ASSERT(mutex.owner == tasks[1]);
    osMutexUnlock(&mutex);

    mark('h');
}

// order

static void waiter(void *param)
{
    (void)param;

    osMutexLock(&mutex);
    mark('0' + osTaskGetPriority(NULL));
    osMutexUnlock(&mutex);
}

// owner which is gone

#if OS_CFG_MUTEX_INHERITANCE
static void hoarder(void *param)
{
    (void)param;

    osMutexLock(&mutex);
    osMutexLock(&second);
    mark('o');

    // never comes back, neither do the mutexes
    osTaskSuspend(NULL);
    mark('!');
}

static void quitter(void *param)
{
    (void)param;

    // leaves with the mutex locked
    osMutexLock(&mutex);
    mark('q');
}
#endif

// two mutexes

static void twoFirst(void *param)
{
    (void)param;

    osDelay(2);
    osMutexLock(&mutex);
    mark('a');
    osMutexUnlock(&mutex);
}

static void twoSecond(void *param)
{
    (void)param;

    osDelay(4);
    osMutexLock(&second);
    mark('b');
    osMutexUnlock(&second);
}

static void twoOwner(void *param)
{
    (void)param;

    osMutexLock(&mutex);
    osMutexLock(&second);
    testWork(10);

#if OS_CFG_MUTEX_INHERITANCE
    // the most important task waiting for any of the mutexes counts
    TEST_ASSERT(priority(0) == 5);
    osMutexUnlock(&second);
    TEST_ASSERT(priority(0) == 3);
    osMutexUnlock(&mutex);
    TEST_ASSERT(priority(0) == 1);
#else
    osMutexUnlock(&second);
    osMutexUnlock(&mutex);
#endif

    mark('o');
}

static void mainTask(void *param)
{
    (void)param;

    // mutex filled with zeros is ready to use, tasks of different priorities fight for it
    spawn(0, worker, 1);
    spawn(1, worker, 2);
    spawn(2, worker, 3);
    spawn(3, worker, 2);
    join();

    TEST_ASSERT(shared == 400);
    TEST_ASSERT(mutex.owner == NULL && mutex.waiters == NULL);

    // task of low priority owns the mutex a task of high priority waits for.
    // task of medium priority must not get in the way, as it would do without
    // priority inheritance: the most important task would wait for it as well.
    spawn(0, inversionLow, 1);
    spawn(1, inversionMid, 2);
    spawn(2, inversionHigh, 3);
    join();

#if OS_CFG_AGING
    // aging lets less important tasks in from time to time, so the order is
    // not carved in stone - but high still gets the mutex before mid is done
    TEST_ASSERT(orders == 6);
    TEST_ASSERT(position('H') < position('h') && position('h') < position('m'));
    orders = 0;
#elif OS_CFG_MUTEX_INHERITANCE
    expect("LHhMml");
#else
    expect("LHMmhl");
#endif

#if OS_CFG_MUTEX_INHERITANCE
    // high waits for mid, mid waits for low: priority goes down the whole chain
    spawn(0, chainLow, 1);
    spawn(1, chainMid, 2);
    spawn(2, chainHigh, 3);

    osDelay(6);
    TEST_ASSERT(priority(0) == 3);
    TEST_ASSERT(priority(1) == 3);
    TEST_ASSERT(osTaskGetPriority(tasks[0]) == 1);
    TEST_ASSERT(osTaskGetPriority(tasks[1]) == 2);

    join();
    expect("LMHmhl");
#endif

    // timeout
    spawn(0, timeoutLow, 1);
    spawn(1, timeoutHigh, 3);
    join();
    expect("h");

    // the most important task gets the mutex first, first come first served otherwise
    osMutexLock(&mutex);
    spawn(0, waiter, 2);
    spawn(1, waiter, 4);
    spawn(2, waiter, 3);
    spawn(3, waiter, 4);
    osDelay(2);

    // we are more important than any of them, so nothing changes for us
    TEST_ASSERT(mutex.waiters == tasks[1]);
    TEST_ASSERT(osCurrentTask->priority == 10);

    // unlocking a mutex which belongs to somebody else does nothing
    osMutexUnlock(&second);
    osMutexLock(&second);
    osMutexUnlock(&second);

    osMutexUnlock(&mutex);
    TEST_ASSERT(mutex.owner == tasks[1]);
    join();
    expect("4432");

    // priority follows the mutexes which are still locked
    spawn(0, twoOwner, 1);
    spawn(1, twoFirst, 3);
    spawn(2, twoSecond, 5);
    join();
    expect("bao");

    TEST_ASSERT(mutex.owner == NULL && mutex.waiters == NULL);
    TEST_ASSERT(second.owner == NULL && second.waiters == NULL);

#if OS_CFG_MUTEX_INHERITANCE
    // mutexes of a task which gets deleted go to whoever waits for them
    spawn(0, hoarder, 3);
    spawn(1, twoFirst, 20);
    spawn(2, twoSecond, 2);
    osDelay(6);

    TEST_ASSERT(mutex.owner == tasks[0] && second.owner == tasks[0]);
    TEST_ASSERT(mutex.waiters == tasks[1] && second.waiters == tasks[2]);
    TEST_ASSERT(priority(0) == 20);

    // the one which is more important than we are takes over right away
    osTaskDelete(tasks[0]);
    TEST_ASSERT(osTaskGetState(tasks[1]) == OS_TASK_DEAD);
    TEST_ASSERT(mutex.owner == NULL && second.owner == tasks[2]);
    tasks[0] = NULL;
    join();
    expect("oab");

    // and so do the mutexes of a task which leaves on its own
    spawn(0, quitter, 3);
    osDelay(1);
    TEST_ASSERT(osTaskGetState(tasks[0]) == OS_TASK_DEAD);
    TEST_ASSERT(mutex.owner == NULL);
    TEST_ASSERT(osMutexTryLock(&mutex, OS_NO_WAIT));
    osMutexUnlock(&mutex);
    tasks[0] = NULL;
    expect("q");
#endif

#if OS_CFG_DYNAMIC
    OsMutex *dynamic = osMutexCreate();
    TEST_ASSERT(dynamic != NULL);
    TEST_ASSERT(osMutexTryLock(dynamic, OS_NO_WAIT));
    osMutexUnlock(dynamic);
    osMutexDestroy(dynamic);
#endif

    testPass();
}

int main(void)
{
    osInit();
    osMutexInit(&second);

    osTaskCreate(mainTask, NULL, memoryMain, sizeof(memoryMain), 10);
    osRun();
}
