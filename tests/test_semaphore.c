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

// Semaphore: counting, limit, timeouts, order of waiting tasks, interrupts.

// VARIANT: noaging -DOS_CFG_AGING=0

#include <string.h>
#include "test.h"

#define TASKS 3

static uint8_t memoryMain[OS_TASK_MEMORY(TEST_STACK)];
static uint8_t memory[TASKS][OS_TASK_MEMORY(TEST_STACK)];
static OsTask *tasks[TASKS];

static OsSemaphore semaphore;
static OsSemaphore signal;

static volatile char order[16];
static volatile uint8_t orders = 0;

static volatile uint16_t given = 0;
static volatile uint16_t taken = 0;
static volatile uint16_t givenAt = 0;
static volatile uint16_t latencyMin = 0xffff;
static volatile uint16_t latencyMax = 0;

static void mark(char id)
{
    OS_CRITICAL {
        order[orders++] = id;
        order[orders] = 0;
    }
}

static void waiter(void *param)
{
    (void)param;

    TEST_ASSERT(osSemaphoreTake(&semaphore, OS_WAIT_FOREVER));
    mark('0' + osTaskGetPriority(NULL));
}

// interrupt gives, task takes
OS_ISR(TIMER1_COMPA_vect)
{
    given++;
    givenAt = testCycles();
    osSemaphoreGiveFromISR(&signal);
}

static void handler(void *param)
{
    (void)param;

    while(1) {
        TEST_ASSERT(osSemaphoreTake(&signal, OS_WAIT_FOREVER));

        // time between the interrupt and this very moment, cycles
        uint16_t cycles = testCycles() - givenAt;
        if(cycles < latencyMin)
            latencyMin = cycles;

        if(cycles > latencyMax)
            latencyMax = cycles;

        taken++;
    }
}

static void mainTask(void *param)
{
    (void)param;

    // counting
    osSemaphoreInit(&semaphore, 2, 5);
    TEST_ASSERT(osSemaphoreCount(&semaphore) == 2);
    TEST_ASSERT(osSemaphoreTake(&semaphore, OS_NO_WAIT));
    TEST_ASSERT(osSemaphoreTake(&semaphore, 10));
    TEST_ASSERT(!osSemaphoreTake(&semaphore, OS_NO_WAIT));

    // timeout
    OsTick start = osTickCount();
    TEST_ASSERT(!osSemaphoreTake(&semaphore, 5));
    TEST_ASSERT((OsTick)(osTickCount() - start) == 5);
    TEST_ASSERT(semaphore.waiters == NULL);

    // limit
    for(uint8_t i = 0; i < 8; i++)
        osSemaphoreGive(&semaphore);

    TEST_ASSERT(osSemaphoreCount(&semaphore) == 5);

    // the most important task first, first come first served otherwise
    osSemaphoreInit(&semaphore, 0, 5);
    tasks[0] = osTaskCreate(waiter, NULL, memory[0], sizeof(memory[0]), 2);
    tasks[1] = osTaskCreate(waiter, NULL, memory[1], sizeof(memory[1]), 4);
    tasks[2] = osTaskCreate(waiter, NULL, memory[2], sizeof(memory[2]), 3);
    osDelay(2);

    TEST_ASSERT(semaphore.waiters == tasks[1]);

    // nobody is more important than we are, they have to wait until we sleep
    osSemaphoreGive(&semaphore);
    osSemaphoreGive(&semaphore);
    TEST_ASSERT(orders == 0);
    TEST_ASSERT(osSemaphoreCount(&semaphore) == 0);
    TEST_ASSERT(osTaskGetState(tasks[1]) == OS_TASK_READY);
    TEST_ASSERT(osTaskGetState(tasks[0]) == OS_TASK_BLOCKED);

    osDelay(2);
    TEST_ASSERT(strcmp((const char*)order, "43") == 0);

    // this time the task which waits is more important than we are: it goes first
    osTaskSetPriority(NULL, 1);
    osSemaphoreGive(&semaphore);
    TEST_ASSERT(strcmp((const char*)order, "432") == 0);
    osTaskSetPriority(NULL, 10);

    // interrupts: nothing gets lost, the task runs right after the interrupt
    osSemaphoreInit(&signal, 0, 1);
    testCyclesStart();
    tasks[0] = osTaskCreate(handler, NULL, memory[0], sizeof(memory[0]), 20);
    testInterruptStart(1777);

    osDelay(500);

    testInterruptStop();
    osDelay(1);

    testNumber("given", given);
    testNumber("taken", taken);
    testNumber("latency min", latencyMin);
    testNumber("latency max", latencyMax);

    TEST_ASSERT(given > 1000);
    TEST_ASSERT(taken == given);

    // there is no waiting for the tick, which would take thousands of cycles
    // the worst case is the tick being handled right after our interrupt
    TEST_ASSERT_FAST(latencyMax < 1000);

    osTaskDelete(tasks[0]);

#if OS_CFG_DYNAMIC
    OsSemaphore *dynamic = osSemaphoreCreate(1, 1);
    TEST_ASSERT(dynamic != NULL);
    TEST_ASSERT(osSemaphoreTake(dynamic, OS_NO_WAIT));
    TEST_ASSERT(!osSemaphoreTake(dynamic, OS_NO_WAIT));
    osSemaphoreDestroy(dynamic);
#endif

    testPass();
}

int main(void)
{
    osInit();

    osTaskCreate(mainTask, NULL, memoryMain, sizeof(memoryMain), 10);
    osRun();
}
