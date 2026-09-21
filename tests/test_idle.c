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

// idle task: it sleeps, it never waits (no matter who asks it to) and it is
// not going anywhere. Waiting where it is not possible is the most common
// mistake there is, it must not take the whole system down.

#include <avr/sleep.h>
#include "test.h"

static uint8_t memoryMain[OS_TASK_MEMORY(TEST_STACK)];

static OsSemaphore semaphore;
static OsMutex mutex;
static OsTimer timer;

static volatile uint32_t idleLoops = 0;
static volatile uint8_t hookTaken = 0xff;
static volatile uint8_t timerTaken = 0xff;
static volatile uint8_t timerCalls = 0;
static volatile OsTick timerTook = 0;

// takes the place of the one which comes with the system
void osIdleHook(void)
{
    idleLoops++;

    // neither of these is going to wait
    osDelay(1000);
    hookTaken = osSemaphoreTake(&semaphore, 1000);

    // sleeps for real as long as osRun did its job
    sleep_cpu();
}

// called by the tick while everybody is asleep, so the idle task is the current one
static void waits(void *param)
{
    (void)param;

    OsTick before = osTickCount();
    osDelay(50);
    timerTaken = osSemaphoreTake(&semaphore, 50);
    timerTook = osTickCount() - before;

    timerCalls++;
}

static void mainTask(void *param)
{
    (void)param;

#if OS_CFG_IDLE_SLEEP
    // idle mode, enabled
    TEST_ASSERT(SMCR == (1 << SE));
#endif

    // we are woken up right after the tick
    osDelay(1);

    OsTick start = osTickCount();
    uint32_t loops = idleLoops;

    osTimerStart(&timer, 5, 0, waits, NULL);
    osDelay(100);

    // nobody made anybody wait, we are back on time
    TEST_ASSERT((OsTick)(osTickCount() - start) == 100);
    TEST_ASSERT(timerCalls == 1);
    TEST_ASSERT(timerTaken == false && timerTook == 0);
    TEST_ASSERT(hookTaken == false);

    // the idle task went around once per interrupt, more or less. It would
    // be thousands of times if the processor did not sleep.
    loops = idleLoops - loops;
    testNumber("idle loops in 100 ticks", loops);
    TEST_ASSERT(loops >= 90);

#if OS_CFG_IDLE_SLEEP
    TEST_ASSERT(loops <= 250);
#endif

    // it cannot be removed
    osTaskSuspend(&osIdleTask);
    osTaskDelete(&osIdleTask);
    TEST_ASSERT(osTaskGetState(&osIdleTask) == OS_TASK_READY);

    loops = idleLoops;
    osDelay(10);
    TEST_ASSERT(idleLoops > loops);

    // nor does its stack suffer from all of the above
#if OS_CFG_STACK_CHECK
    TEST_ASSERT(osTaskStackFree(&osIdleTask) > 64);
#endif

    testPass();
}

int main(void)
{
    osInit();
    osSemaphoreInit(&semaphore, 0, 1);

    // there is no waiting before the system is running either
    OsTick before = osTickCount();
    osDelay(100);
    TEST_ASSERT(!osSemaphoreTake(&semaphore, 100));
    TEST_ASSERT(osTickCount() == before);

    // what does not need to wait works as usual
    osSemaphoreGive(&semaphore);
    TEST_ASSERT(osSemaphoreTake(&semaphore, 100));
    TEST_ASSERT(osMutexTryLock(&mutex, 100));

    // locked already, by us: waiting for ourselves would take forever
    TEST_ASSERT(!osMutexTryLock(&mutex, 100));
    osMutexUnlock(&mutex);
    TEST_ASSERT(mutex.owner == NULL);

    osTaskSuspend(NULL);
    osTaskDelete(NULL);

    osTaskCreate(mainTask, NULL, memoryMain, sizeof(memoryMain), 10);
    osRun();
}
