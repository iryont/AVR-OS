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

// Tasks: parameters, order of execution, delays, round robin, yield, exit.

// VARIANT: minimal -DOS_CFG_AGING=0 -DOS_CFG_MUTEX_INHERITANCE=0 -DOS_CFG_STACK_CHECK=0 -DOS_CFG_DYNAMIC=0 -DOS_CFG_TIMERS=0 -DOS_CFG_IDLE_SLEEP=0
// VARIANT: tick32 -DOS_CFG_TICK_32BIT=1
// VARIANT: timer2 -DOS_CFG_TICK_TIMER=2
// VARIANT: timer1 -DOS_CFG_TICK_TIMER=1 -DOS_CFG_TICK_HZ=250

#include "test.h"

static uint8_t memoryHigh[OS_TASK_MEMORY(TEST_STACK)];
static uint8_t memoryMid[OS_TASK_MEMORY(TEST_STACK)];
static uint8_t memoryMain[OS_TASK_MEMORY(TEST_STACK)];
static uint8_t memoryA[OS_TASK_MEMORY(TEST_STACK)];
static uint8_t memoryB[OS_TASK_MEMORY(TEST_STACK)];

static OsTask *taskHigh;
static OsTask *taskMid;

static volatile uint8_t order[32];
static volatile uint8_t orders = 0;

static volatile uint32_t spins[2];
static volatile uint8_t started = 0;

static void mark(uint8_t id)
{
    OS_CRITICAL {
        order[orders++] = id;
    }
}

static void high(void *param)
{
    TEST_ASSERT(param == (void*)0x1234);

    mark(1);
    osDelay(10);
    mark(5);

    // task is gone once its function returns
}

static void mid(void *param)
{
    TEST_ASSERT(param == (void*)0xabcd);

    mark(2);
    osDelay(5);
    mark(4);

    osTaskExit();
}

static void spin(void *param)
{
    uint8_t id = (uint8_t)(uintptr_t)param;

    while(1) {
        OS_CRITICAL {
            spins[id]++;
        }
    }
}

static void yielder(void *param)
{
    uint8_t id = (uint8_t)(uintptr_t)param;

    for(uint8_t i = 0; i < 8; i++) {
        mark(id);
        osYield();
    }
}

static void starter(void *param)
{
    started = (uint8_t)(uintptr_t)param;
}

static void mainTask(void *param)
{
    (void)param;

    // more important tasks went first and they are asleep now
    mark(3);
    TEST_ASSERT(osTaskGetState(taskHigh) == OS_TASK_BLOCKED);
    TEST_ASSERT(osTaskGetState(taskMid) == OS_TASK_BLOCKED);
    TEST_ASSERT(osTaskGetState(NULL) == OS_TASK_RUNNING);
    TEST_ASSERT(osTaskCurrent() != taskHigh && osTaskCurrent() != taskMid);

    osDelay(20);

    TEST_ASSERT(orders == 5);
    for(uint8_t i = 0; i < 5; i++)
        TEST_ASSERT(order[i] == i + 1);

    TEST_ASSERT(osTaskGetState(taskHigh) == OS_TASK_DEAD);
    TEST_ASSERT(osTaskGetState(taskMid) == OS_TASK_DEAD);

    // we are woken up right after the tick, so delays below are exact
    OsTick start = osTickCount();
    osDelay(7);
    TEST_ASSERT((OsTick)(osTickCount() - start) == 7);

    start = osTickCount();
    osDelay(1);
    TEST_ASSERT((OsTick)(osTickCount() - start) == 1);

    // delay of zero is just a yield
    start = osTickCount();
    osDelay(0);
    TEST_ASSERT(osTickCount() == start);

    // tasks with the same priority share the processor
    OsTask *a = osTaskCreate(spin, (void*)0, memoryA, sizeof(memoryA), 1);
    OsTask *b = osTaskCreate(spin, (void*)1, memoryB, sizeof(memoryB), 1);
    TEST_ASSERT(a && b);
    TEST_ASSERT(osTaskGetState(a) == OS_TASK_READY);

    osDelay(100);

    // tasks which got deleted do not run anymore. They have to go before we
    // do anything which takes time: they are less important than we are, but
    // aging would let them in sooner or later.
    osTaskDelete(a);
    osTaskDelete(b);
    TEST_ASSERT(osTaskGetState(a) == OS_TASK_DEAD);

    // deleting a task twice is harmless
    osTaskDelete(a);

    uint32_t spinsA, spinsB;
    OS_CRITICAL {
        spinsA = spins[0];
        spinsB = spins[1];
    }

    testNumber("spins a", spinsA);
    testNumber("spins b", spinsB);

    TEST_ASSERT(spinsA > 1000 && spinsB > 1000);
    uint32_t difference = spinsA > spinsB ? spinsA - spinsB : spinsB - spinsA;
    TEST_ASSERT(difference < (spinsA + spinsB) / 20);

    osDelay(5);

    OS_CRITICAL {
        TEST_ASSERT(spins[0] == spinsA && spins[1] == spinsB);
    }

    // yield gives the processor to the other task with the same priority
    orders = 0;
    osTaskCreate(yielder, (void*)1, memoryA, sizeof(memoryA), 1);
    osTaskCreate(yielder, (void*)2, memoryB, sizeof(memoryB), 1);
    osDelay(3);

    // they take turns. The tick makes them take turns as well, so whenever it
    // gets between a mark and the yield which follows, the order is disturbed.
    TEST_ASSERT(orders == 16);

    uint8_t turns = 0;
    for(uint8_t i = 1; i < 16; i++) {
        if(order[i] != order[i - 1])
            turns++;
    }

    TEST_ASSERT(turns >= 12);

    // periodic delay does not care how long the work takes
    start = osTickCount();

    OsTick wake = start;
    for(uint8_t i = 1; i <= 5; i++) {
        osDelayUntil(&wake, 10);
        TEST_ASSERT(osTickCount() == (OsTick)(start + 10 * i));
        TEST_ASSERT(wake == (OsTick)(start + 10 * i));

        testWork(1 + i);
    }

    // nor does it wait if the deadline has been missed already
    testWork(15);
    OsTick before = osTickCount();
    osDelayUntil(&wake, 10);
    TEST_ASSERT((OsTick)(osTickCount() - before) <= 1);
    TEST_ASSERT(wake == (OsTick)(start + 60));

    // more important task gets the processor as soon as it is created
    started = 0;
    OsTask *task = osTaskCreate(starter, (void*)7, memoryA, sizeof(memoryA), 200);
    TEST_ASSERT(task != NULL);
    TEST_ASSERT(started == 7);
    TEST_ASSERT(osTaskGetState(task) == OS_TASK_DEAD);

    // less important one has to wait for it
    task = osTaskCreate(starter, (void*)9, memoryA, sizeof(memoryA), 1);
    TEST_ASSERT(started == 7);
    osDelay(2);
    TEST_ASSERT(started == 9);

    // priority is kept within the limits
    task = osTaskCreate(starter, NULL, memoryA, sizeof(memoryA), 0);
    TEST_ASSERT(osTaskGetPriority(task) == OS_PRIORITY_MIN);
    osDelay(2);

    // too small to be a task
    TEST_ASSERT(osTaskCreate(starter, NULL, memoryA, sizeof(OsTask) + 8, 1) == NULL);
    TEST_ASSERT(osTaskCreate(starter, NULL, NULL, sizeof(memoryA), 1) == NULL);

    testPass();
}

int main(void)
{
    osInit();

    taskHigh = osTaskCreate(high, (void*)0x1234, memoryHigh, sizeof(memoryHigh), 4);
    taskMid = osTaskCreate(mid, (void*)0xabcd, memoryMid, sizeof(memoryMid), 3);
    osTaskCreate(mainTask, NULL, memoryMain, sizeof(memoryMain), 2);

    // nothing runs until the system does
    TEST_ASSERT(taskHigh && taskMid);
    TEST_ASSERT(orders == 0);

    osRun();
}
