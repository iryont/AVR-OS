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

// how long things take, in processor cycles. The numbers are printed (use -v
// to see them) and compared with limits, so nobody makes them worse unnoticed.

// VARIANT: minimal -DOS_CFG_AGING=0 -DOS_CFG_MUTEX_INHERITANCE=0 -DOS_CFG_STACK_CHECK=0 -DOS_CFG_DYNAMIC=0 -DOS_CFG_TIMERS=0

#include <util/delay_basic.h>
#include "test.h"

#define ROUNDS 200

static uint8_t memoryMain[OS_TASK_MEMORY(TEST_STACK)];
static uint8_t memory[2][OS_TASK_MEMORY(TEST_STACK)];

static OsSemaphore semaphore;
static OsMutex mutex;
static OsQueue queue;
static uint8_t queueBuffer[OS_QUEUE_MEMORY(sizeof(uint16_t), 4)];

static volatile uint16_t stamp;
static volatile uint16_t wakeup = 0xffff;
static volatile uint16_t latency;

static void yielder(void *param)
{
    (void)param;

    while(1)
        osYield();
}

static void taker(void *param)
{
    (void)param;

    while(1) {
        osSemaphoreTake(&semaphore, OS_WAIT_FOREVER);
        // the smallest one is the one nobody interrupted
        uint16_t cycles = testCycles() - stamp;
        if(cycles < wakeup)
            wakeup = cycles;
    }
}

static void receiver(void *param)
{
    (void)param;

    while(1) {
        uint16_t message;
        osQueueReceive(&queue, &message, OS_WAIT_FOREVER);
        // the smallest one is the one nobody interrupted
        uint16_t cycles = testCycles() - stamp;
        if(cycles < wakeup)
            wakeup = cycles;
    }
}

OS_ISR(TIMER1_COMPA_vect)
{
    // the timer keeps counting once it fires, which tells how long ago it was
    stamp = testCycles() - TCNT1;
    osSemaphoreGiveFromISR(&semaphore);
}

static void interrupted(void *param)
{
    (void)param;

    while(1) {
        osSemaphoreTake(&semaphore, OS_WAIT_FOREVER);
        latency = testCycles() - stamp;
    }
}

// the smallest one of all the rounds is the one nobody interrupted
// (names are odd on purpose, they must not hide the variables used by the code)
#define MEASURE(result, code)                                   \
    do {                                                        \
        result = 0xffff;                                        \
        for(uint16_t _round = 0; _round < ROUNDS; _round++) {   \
            uint16_t _before = testCycles();                    \
            code;                                               \
            uint16_t _taken = testCycles() - _before;           \
            if(_taken < result)                                 \
                result = _taken;                                \
        }                                                       \
    } while(0)

static void mainTask(void *param)
{
    (void)param;

    uint16_t cycles;
    testCyclesStart();

    // reading the timer takes time as well
    uint16_t overhead;
    MEASURE(overhead, (void)0);
    testNumber("measurement overhead", overhead);

    // nobody to yield to
    MEASURE(cycles, osYield());
    testNumber("yield, no switch", cycles - overhead);
    TEST_ASSERT_FAST(cycles - overhead < 80);

    // two tasks yielding to each other: there and back again
    OsTask *task = osTaskCreate(yielder, NULL, memory[0], sizeof(memory[0]), 10);
    MEASURE(cycles, osYield());
    testNumber("yield, two switches", cycles - overhead);
    TEST_ASSERT_FAST(cycles - overhead < 600);
    osTaskDelete(task);

    // uncontended objects
    MEASURE(cycles, osMutexLock(&mutex); osMutexUnlock(&mutex));
    testNumber("mutex lock + unlock", cycles - overhead);
    TEST_ASSERT_FAST(cycles - overhead < 260);

    osSemaphoreInit(&semaphore, 0, 1);
    MEASURE(cycles, osSemaphoreGive(&semaphore); osSemaphoreTake(&semaphore, OS_NO_WAIT));
    testNumber("semaphore give + take", cycles - overhead);
    TEST_ASSERT_FAST(cycles - overhead < 150);

    uint16_t message = 0;
    osQueueInit(&queue, queueBuffer, sizeof(uint16_t), 4);
    MEASURE(cycles, osQueueSend(&queue, &message, OS_NO_WAIT); osQueueReceive(&queue, &message, OS_NO_WAIT));
    testNumber("queue send + receive", cycles - overhead);
    TEST_ASSERT_FAST(cycles - overhead < 430);

    // more important task waiting for us: wake it up, let it run, get back here
    task = osTaskCreate(taker, NULL, memory[0], sizeof(memory[0]), 20);
    MEASURE(cycles, stamp = testCycles(); osSemaphoreGive(&semaphore));
    testNumber("semaphore give to wake-up", wakeup);
    testNumber("semaphore give, two switches", cycles - overhead);
    TEST_ASSERT_FAST(wakeup < 480);
    TEST_ASSERT_FAST(cycles - overhead < 800);
    osTaskDelete(task);

    wakeup = 0xffff;
    task = osTaskCreate(receiver, NULL, memory[0], sizeof(memory[0]), 20);
    MEASURE(cycles, stamp = testCycles(); osQueueSend(&queue, &message, OS_NO_WAIT));
    testNumber("queue send to wake-up", wakeup);
    testNumber("queue send, two switches", cycles - overhead);
    TEST_ASSERT_FAST(wakeup < 620);
    TEST_ASSERT_FAST(cycles - overhead < 950);
    osTaskDelete(task);

    // interrupt to the task which handles it: from the moment the interrupt
    // has been requested to the first line of the task
    task = osTaskCreate(interrupted, NULL, memory[0], sizeof(memory[0]), 20);
    latency = 0xffff;

    uint16_t best = 0xffff;
    testInterruptStart(9973);
    for(uint8_t i = 0; i < 100; i++) {
        osDelay(1);
        if(latency < best)
            best = latency;
    }
    testInterruptStop();
    osTaskDelete(task);

    testNumber("interrupt to task", best);
    TEST_ASSERT_FAST(best < 520);

    // cost of the tick when there is nothing to do. We are woken up right
    // after the tick, the loop below takes two ticks and a half.
    osDelay(1);

    uint16_t iterations = F_CPU / OS_CFG_TICK_HZ / 4 * 5 / 2;
    uint16_t before = testCycles();
    _delay_loop_2(iterations);
    cycles = testCycles() - before;

    uint16_t tick = (cycles - iterations * 4 - overhead) / 2;
    testNumber("tick, nothing to do", tick);
    TEST_ASSERT_FAST(tick < 190);

    testNumber("size of a task", sizeof(OsTask));
    testNumber("size of a mutex", sizeof(OsMutex));
    testNumber("size of a semaphore", sizeof(OsSemaphore));
    testNumber("size of a queue", sizeof(OsQueue));
    testNumber("size of an event", sizeof(OsEvent));

    testPass();
}

int main(void)
{
    osInit();

    osTaskCreate(mainTask, NULL, memoryMain, sizeof(memoryMain), 10);
    osRun();
}
