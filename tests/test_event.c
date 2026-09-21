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

// Event: waiting for any or all of the flags, clearing, timeouts, interrupts.

#include "test.h"

#define TASKS 3

static uint8_t memoryMain[OS_TASK_MEMORY(TEST_STACK)];
static uint8_t memory[TASKS][OS_TASK_MEMORY(TEST_STACK)];
static OsTask *tasks[TASKS];

// filled with zeros, which makes it ready to use
static OsEvent event;

static volatile uint8_t got[TASKS];
static volatile uint16_t fired = 0;
static volatile uint16_t handled = 0;

static void waitAny(void *param)
{
    (void)param;
    got[0] = osEventWait(&event, 0x03, OS_EVENT_ANY, OS_WAIT_FOREVER);
}

static void waitAll(void *param)
{
    (void)param;
    got[1] = osEventWait(&event, 0x06, OS_EVENT_ALL, OS_WAIT_FOREVER);
}

static void waitKeep(void *param)
{
    (void)param;
    got[2] = osEventWait(&event, 0x80, OS_EVENT_KEEP, OS_WAIT_FOREVER);
}

OS_ISR(TIMER1_COMPA_vect)
{
    fired++;
    osEventSetFromISR(&event, (fired & 1) ? 0x10 : 0x20);
}

static void handler(void *param)
{
    (void)param;

    // both flags are needed, so every second interrupt wakes us up
    while(1) {
        TEST_ASSERT(osEventWait(&event, 0x30, OS_EVENT_ALL, OS_WAIT_FOREVER) == 0x30);
        handled++;
    }
}

static void mainTask(void *param)
{
    (void)param;

    // nothing is set
    TEST_ASSERT(osEventGet(&event) == 0);
    TEST_ASSERT(osEventWait(&event, 0xff, OS_EVENT_ANY, OS_NO_WAIT) == 0);

    OsTick start = osTickCount();
    TEST_ASSERT(osEventWait(&event, 0xff, OS_EVENT_ANY, 5) == 0);
    TEST_ASSERT((OsTick)(osTickCount() - start) == 5);
    TEST_ASSERT(event.waiters == NULL);

    // flags we have been waiting for are cleared, other ones are left alone
    osEventSet(&event, 0x0f);
    TEST_ASSERT(osEventWait(&event, 0x05, OS_EVENT_ANY, OS_NO_WAIT) == 0x05);
    TEST_ASSERT(osEventGet(&event) == 0x0a);

    // all means all
    TEST_ASSERT(osEventWait(&event, 0x0e, OS_EVENT_ALL, OS_NO_WAIT) == 0);
    TEST_ASSERT(osEventGet(&event) == 0x0a);
    TEST_ASSERT(osEventWait(&event, 0x0a, OS_EVENT_ALL, OS_NO_WAIT) == 0x0a);
    TEST_ASSERT(osEventGet(&event) == 0);

    // unless asked to keep them
    osEventSet(&event, 0x41);
    TEST_ASSERT(osEventWait(&event, 0x40, OS_EVENT_KEEP, OS_NO_WAIT) == 0x40);
    TEST_ASSERT(osEventGet(&event) == 0x41);
    osEventClear(&event, 0x40);
    TEST_ASSERT(osEventGet(&event) == 0x01);
    osEventClear(&event, 0xff);

    // tasks waiting for different things
    tasks[0] = osTaskCreate(waitAny, NULL, memory[0], sizeof(memory[0]), 20);
    tasks[1] = osTaskCreate(waitAll, NULL, memory[1], sizeof(memory[1]), 21);
    tasks[2] = osTaskCreate(waitKeep, NULL, memory[2], sizeof(memory[2]), 22);

    for(uint8_t i = 0; i < TASKS; i++)
        TEST_ASSERT(osTaskGetState(tasks[i]) == OS_TASK_BLOCKED);

    // not enough for the task which wants both 0x02 and 0x04
    osEventSet(&event, 0x04);
    TEST_ASSERT(osTaskGetState(tasks[1]) == OS_TASK_BLOCKED);
    TEST_ASSERT(osEventGet(&event) == 0x04);

    // the task which wants all of them is more important, so it is served first
    // and it takes 0x02 away. The other one is fine with 0x01 though.
    osEventSet(&event, 0x03);
    TEST_ASSERT(got[1] == 0x06);
    TEST_ASSERT(got[0] == 0x01);
    TEST_ASSERT(osEventGet(&event) == 0);
    TEST_ASSERT(osTaskGetState(tasks[2]) == OS_TASK_BLOCKED);

    osEventSet(&event, 0x80);
    TEST_ASSERT(got[2] == 0x80);
    TEST_ASSERT(osEventGet(&event) == 0x80);
    TEST_ASSERT(event.waiters == NULL);
    osEventClear(&event, 0x80);

    // interrupts
    tasks[0] = osTaskCreate(handler, NULL, memory[0], sizeof(memory[0]), 20);
    testInterruptStart(2203);
    osDelay(300);
    testInterruptStop();
    osDelay(1);

    testNumber("fired", fired);
    testNumber("handled", handled);

    TEST_ASSERT(fired > 1000);
    TEST_ASSERT(handled == fired / 2);
    osTaskDelete(tasks[0]);

#if OS_CFG_DYNAMIC
    OsEvent *dynamic = osEventCreate();
    TEST_ASSERT(dynamic != NULL);
    osEventSet(dynamic, 0x01);
    TEST_ASSERT(osEventWait(dynamic, 0x01, OS_EVENT_ANY, OS_NO_WAIT) == 0x01);
    osEventDestroy(dynamic);
#endif

    testPass();
}

int main(void)
{
    osInit();

    osTaskCreate(mainTask, NULL, memoryMain, sizeof(memoryMain), 10);
    osRun();
}
