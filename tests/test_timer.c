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

// software timers, and time in general: everything which deals with ticks is
// executed once again with the tick counter about to overflow.

// VARIANT: overflow -DOS_CFG_TICK_START=65500
// VARIANT: overflow32 -DOS_CFG_TICK_32BIT=1 -DOS_CFG_TICK_START=4294967250
// VARIANT: high@atmega2560 tests/filler.c -Wl,--undefined=highFiller

#include "test.h"

static uint8_t memoryMain[OS_TASK_MEMORY(TEST_STACK)];
static uint8_t memory[3][OS_TASK_MEMORY(TEST_STACK)];

static OsTimer once;
static OsTimer periodic;
static OsTimer other;

static OsSemaphore semaphore;

static volatile OsTick firedAt[8];
static volatile uint8_t fires = 0;
static volatile uint8_t others = 0;
static volatile uint8_t restarts = 0;

static volatile OsTick wokenAt[3];

static void fire(void *param)
{
    TEST_ASSERT(param == (void*)0x55aa);

    if(fires < 8)
        firedAt[fires++] = osTickCount();
}

static void count(void *param)
{
    (*(volatile uint8_t*)param)++;
}

// timers can be stopped and started by their own functions
static void stopper(void *param)
{
    (void)param;

    if(++others == 3)
        osTimerStop(&other);
}

static void restarter(void *param)
{
    (void)param;

    if(++restarts < 4)
        osTimerStart(&other, 2 * restarts, 0, restarter, NULL);
}

static void giver(void *param)
{
    (void)param;
    osSemaphoreGiveFromISR(&semaphore);
}

static void sleeper(void *param)
{
    uint8_t id = (uint8_t)(uintptr_t)param;

    // the longer the delay the later we wake up, overflow of the counter or not
    osDelay(20 + 30 * id);
    wokenAt[id] = osTickCount();
}

static void mainTask(void *param)
{
    (void)param;

    // we are woken up right after the tick
    osDelay(1);
    OsTick start = osTickCount();

    // tasks which sleep while the tick counter overflows
    for(uint8_t i = 0; i < 3; i++)
        osTaskCreate(sleeper, (void*)(uintptr_t)(2 - i), memory[i], sizeof(memory[i]), 20);

    // one shot
    TEST_ASSERT(!osTimerActive(&once));
    osTimerStart(&once, 5, 0, fire, (void*)0x55aa);
    TEST_ASSERT(osTimerActive(&once));

    osDelay(4);
    TEST_ASSERT(fires == 0);
    osDelay(1);
    TEST_ASSERT(fires == 1);
    TEST_ASSERT(firedAt[0] == (OsTick)(start + 5));
    TEST_ASSERT(!osTimerActive(&once));

    osDelay(20);
    TEST_ASSERT(fires == 1);

    // periodic, the first period is different
    fires = 0;
    start = osTickCount();
    osTimerStart(&periodic, 3, 10, fire, (void*)0x55aa);
    osDelay(45);
    osTimerStop(&periodic);

    TEST_ASSERT(fires == 5);
    for(uint8_t i = 0; i < 5; i++)
        TEST_ASSERT(firedAt[i] == (OsTick)(start + 3 + 10 * i));

    TEST_ASSERT(!osTimerActive(&periodic));
    osDelay(20);
    TEST_ASSERT(fires == 5);

    // sleepers are up by now, each of them on time
    for(uint8_t i = 0; i < 3; i++)
        TEST_ASSERT((OsTick)(wokenAt[i] - wokenAt[0]) == 30 * i);

    // timer which is started again starts over
    fires = 0;
    start = osTickCount();
    osTimerStart(&once, 10, 0, fire, (void*)0x55aa);
    osDelay(6);
    osTimerStart(&once, 10, 0, fire, (void*)0x55aa);
    osDelay(6);
    TEST_ASSERT(fires == 0);
    osDelay(10);
    TEST_ASSERT(fires == 1);
    TEST_ASSERT(firedAt[0] == (OsTick)(start + 16));

    // stopping a timer which is not running is harmless
    osTimerStop(&once);
    osTimerStop(&once);

    // several timers at once, some of them expire with the very same tick
    uint8_t a = 0, b = 0;
    osTimerStart(&once, 4, 4, count, &a);
    osTimerStart(&periodic, 8, 8, count, &b);
    osTimerStart(&other, 1, 1, stopper, NULL);
    osDelay(41);
    osTimerStop(&once);
    osTimerStop(&periodic);

    TEST_ASSERT(a == 10);
    TEST_ASSERT(b == 5);
    TEST_ASSERT(others == 3);
    TEST_ASSERT(!osTimerActive(&other));

    // timer which starts itself over: 1 + 2 + 4 + 6 ticks
    start = osTickCount();
    osTimerStart(&other, 1, 0, restarter, NULL);
    osDelay(12);
    TEST_ASSERT(restarts == 3);
    TEST_ASSERT(osTimerActive(&other));
    osDelay(2);
    TEST_ASSERT(restarts == 4);
    TEST_ASSERT(!osTimerActive(&other));

    // delay of zero means the next tick
    fires = 0;
    start = osTickCount();
    osTimerStart(&once, 0, 0, fire, (void*)0x55aa);
    osDelay(1);
    TEST_ASSERT(fires == 1 && firedAt[0] == (OsTick)(start + 1));

    // timer wakes a task up
    osSemaphoreInit(&semaphore, 0, 1);
    start = osTickCount();
    osTimerStart(&once, 7, 0, giver, NULL);
    TEST_ASSERT(osSemaphoreTake(&semaphore, 100));
    TEST_ASSERT((OsTick)(osTickCount() - start) == 7);

    // periodic delay across the overflow
    OsTick wake = osTickCount();
    start = wake;
    for(uint8_t i = 1; i <= 10; i++) {
        osDelayUntil(&wake, 15);
        TEST_ASSERT(osTickCount() == (OsTick)(start + 15 * i));
    }

    testPass();
}

int main(void)
{
    osInit();

    osTaskCreate(mainTask, NULL, memoryMain, sizeof(memoryMain), 10);
    osRun();
}
