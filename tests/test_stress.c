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

// everything at once, for a long time and with a tick way faster than usual:
// whatever depends on a lucky (or unlucky) timing is going to show up here.

// FLAGS: -DOS_CFG_TICK_HZ=10000
// VARIANT: noaging -DOS_CFG_AGING=0
// VARIANT: norobin -DOS_CFG_ROUND_ROBIN=0
// VARIANT: tick32 -DOS_CFG_TICK_32BIT=1 -DOS_CFG_TICK_START=4294960000
// VARIANT: overflow -DOS_CFG_TICK_START=60000
// TIMEOUT: 600

#include <string.h>
#include "test.h"

#define DURATION 30000

typedef struct {
    uint8_t sender;
    uint16_t number;
} Message;

static uint8_t memoryMain[OS_TASK_MEMORY(TEST_STACK)];
static uint8_t memoryProducer[2][OS_TASK_MEMORY(TEST_STACK)];
static uint8_t memoryConsumer[OS_TASK_MEMORY(TEST_STACK)];
static uint8_t memoryWorker[3][OS_TASK_MEMORY(TEST_STACK)];
static uint8_t memoryHandler[OS_TASK_MEMORY(TEST_STACK)];
static uint8_t memoryListener[OS_TASK_MEMORY(TEST_STACK)];
static uint8_t memoryChaos[OS_TASK_MEMORY(TEST_STACK)];
static uint8_t memorySpinner[OS_TASK_MEMORY(TEST_STACK)];

static OsTask *producers[2];
static OsTask *workers[3];
static OsTask *spinner;
static OsTask *everyone[12];
static uint8_t everyones = 0;

static OsQueue queue;
static uint8_t queueBuffer[OS_QUEUE_MEMORY(sizeof(Message), 4)];

static OsMutex mutex;
static OsSemaphore semaphore;
static OsEvent event;
static OsTimer periodic;
static OsTimer oneShot;

static volatile uint8_t running = 1;

static volatile uint16_t sent[2];
static volatile uint16_t received[2];
static volatile uint16_t timeouts = 0;

static volatile uint32_t first = 0;
static volatile uint32_t second = 0;
static volatile uint16_t locked[3];
static volatile uint16_t gaveUp = 0;

static volatile uint16_t lateness = 0;
static volatile uint16_t given = 0;
static volatile uint16_t taken = 0;
static volatile uint16_t flagged = 0;
static volatile uint16_t listened = 0;

static volatile uint16_t periods = 0;
static volatile uint16_t shots = 0;

static volatile uint32_t spins = 0;
static volatile uint16_t created = 0;
static volatile uint16_t finished = 0;
static volatile uint16_t refused = 0;

static OsTask *spawn(void (*function)(void*), void *param, uint8_t *memory, uint8_t priority)
{
    OsTask *task = osTaskCreate(function, param, memory, OS_TASK_MEMORY(TEST_STACK), priority);
    TEST_ASSERT(task != NULL);

    everyone[everyones++] = task;
    return task;
}

static void producer(void *param)
{
    uint8_t id = (uint8_t)(uintptr_t)param;

    while(running) {
        Message message = { id, sent[id] };

        // the queue is full most of the time
        if(osQueueSend(&queue, &message, 50))
            sent[id]++;
        else
            timeouts++;

        if(sent[id] % 64 == 0)
            osDelay(1 + id);
    }
}

static void consumer(void *param)
{
    (void)param;

    while(1) {
        Message message;
        if(!osQueueReceive(&queue, &message, 100)) {
            if(!running)
                break;

            continue;
        }

        // nothing is lost, nothing comes twice, nothing comes out of order
        TEST_ASSERT(message.sender < 2);
        TEST_ASSERT(message.number == received[message.sender]);
        received[message.sender]++;

        if(message.number % 128 == 0)
            osDelay(3);
    }
}

static void worker(void *param)
{
    uint8_t id = (uint8_t)(uintptr_t)param;
    uint16_t seed = 12345 + id;

    while(running) {
        seed = seed * 25173 + 13849;

        // the most important worker is not that patient
        if(id == 2) {
            if(!osMutexTryLock(&mutex, 2)) {
                gaveUp++;
                osDelay(1);
                continue;
            }
        } else {
            osMutexLock(&mutex);
        }

        // both numbers are equal whenever the mutex is ours
        TEST_ASSERT(first == second);
        first++;

        switch((seed >> 8) % 4) {
            case 0:
                osYield();
                break;

            case 1:
                osDelay(1);
                break;

            case 2:
                testWork(1);
                break;

            default:
                break;
        }

        second++;
        TEST_ASSERT(first == second);
        locked[id]++;

        osMutexUnlock(&mutex);

        if((seed >> 12) % 4 == 0)
            osDelay(id + 1);
    }
}

OS_ISR(TIMER1_COMPA_vect)
{
    // the timer keeps counting cycles once it fires, so this is how long the
    // interrupt had to wait: for the kernel to enable interrupts again or for
    // the tick to be handled, whichever takes longer.
    uint16_t late = TCNT1;
    if(late > lateness)
        lateness = late;

    given++;
    osSemaphoreGiveFromISR(&semaphore);

    if(given % 3 == 0) {
        flagged++;
        osEventSetFromISR(&event, (flagged & 1) ? 0x01 : 0x02);
    }
}

static void handler(void *param)
{
    (void)param;

    while(1) {
        if(osSemaphoreTake(&semaphore, 100))
            taken++;
        else if(!running)
            break;
    }
}

static void listener(void *param)
{
    (void)param;

    while(1) {
        uint8_t flags = osEventWait(&event, 0x03, OS_EVENT_ALL, 100);
        if(flags) {
            TEST_ASSERT(flags == 0x03);
            listened++;
        } else if(!running) {
            break;
        }
    }
}

static void tick(void *param)
{
    (void)param;
    periods++;
}

static void shot(void *param)
{
    (void)param;
    shots++;
}

static void spin(void *param)
{
    (void)param;

    while(1) {
        OS_CRITICAL {
            spins++;
        }
    }
}

#if OS_CFG_DYNAMIC
static void shortLived(void *param)
{
    uint16_t ticks = (uint16_t)(uintptr_t)param;

    void *block = osMalloc(24);
    TEST_ASSERT(block != NULL);
    memset(block, 0xcc, 24);

    osDelay(ticks);
    osFree(block);

    OS_CRITICAL {
        finished++;
    }
}
#endif

// makes sure nobody gets too comfortable
static void chaos(void *param)
{
    (void)param;

    uint16_t seed = 4711;
    uint8_t suspended = 0;

    while(running) {
        seed = seed * 25173 + 13849;
        uint8_t choice = seed >> 8;

        switch(choice % 5) {
            case 0:
                osTaskSetPriority(workers[choice % 3], 2 + (choice >> 3) % 5);
                break;

            case 1:
                if(suspended)
                    osTaskResume(spinner);
                else
                    osTaskSuspend(spinner);

                suspended = !suspended;
                break;

            case 2:
#if OS_CFG_DYNAMIC
                if(created - finished < 3) {
                    if(osTaskCreateDynamic(shortLived, (void*)(uintptr_t)(1 + choice % 7), TEST_STACK, 1 + choice % 9))
                        created++;
                    else
                        refused++;
                }
#endif
                break;

            case 3:
                osTimerStart(&oneShot, 1 + choice % 4, 0, shot, NULL);
                break;

            default:
                osTaskSetPriority(producers[choice % 2], 3 + (choice >> 4) % 3);
                break;
        }

        osDelay(1 + (seed >> 12) % 4);
    }

    if(suspended)
        osTaskResume(spinner);
}

static void mainTask(void *param)
{
    (void)param;

#if OS_CFG_DYNAMIC
    void *probe = osMalloc(16);
    TEST_ASSERT(probe != NULL);
    osFree(probe);
#endif

    osQueueInit(&queue, queueBuffer, sizeof(Message), 4);
    osSemaphoreInit(&semaphore, 0, 255);

    producers[0] = spawn(producer, (void*)0, memoryProducer[0], 3);
    producers[1] = spawn(producer, (void*)1, memoryProducer[1], 4);
    OsTask *taskConsumer = spawn(consumer, NULL, memoryConsumer, 5);

    workers[0] = spawn(worker, (void*)0, memoryWorker[0], 2);
    workers[1] = spawn(worker, (void*)1, memoryWorker[1], 3);
    workers[2] = spawn(worker, (void*)2, memoryWorker[2], 6);

    OsTask *taskHandler = spawn(handler, NULL, memoryHandler, 8);
    OsTask *taskListener = spawn(listener, NULL, memoryListener, 7);
    spinner = spawn(spin, NULL, memorySpinner, 1);
    OsTask *taskChaos = spawn(chaos, NULL, memoryChaos, 9);

    osDelay(1);
    OsTick start = osTickCount();

    osTimerStart(&periodic, 7, 7, tick, NULL);
    // a prime number of cycles, so it drifts against the tick all the time
    testInterruptStart(5003);

    osDelay(DURATION);

    // time to go home
    testInterruptStop();
    osTimerStop(&periodic);
    OsTick elapsed = osTickCount() - start;
    running = 0;

    OsTask *leaving[] = { producers[0], producers[1], taskConsumer, workers[0], workers[1], workers[2], taskHandler, taskListener, taskChaos };
    for(uint8_t i = 0; i < sizeof(leaving) / sizeof(leaving[0]); i++) {
        while(osTaskGetState(leaving[i]) != OS_TASK_DEAD)
            osDelay(10);
    }

    osDelay(20);

    testNumber("elapsed", elapsed);
    testNumber("worst interrupt latency", lateness);
    testNumber("sent", (uint32_t)sent[0] + sent[1]);
    testNumber("send timeouts", timeouts);
    testNumber("locked", first);
    testNumber("lock timeouts", gaveUp);
    testNumber("given", given);
    testNumber("flagged", flagged);
    testNumber("periods", periods);
    testNumber("shots", shots);
    testNumber("spins", spins);
    testNumber("dynamic tasks", created);
    testNumber("dynamic tasks finished", finished);
    testNumber("dynamic tasks refused", refused);

    // queue
    TEST_ASSERT_FAST(sent[0] > 100 && sent[1] > 100);
    TEST_ASSERT(received[0] == sent[0] && received[1] == sent[1]);
    TEST_ASSERT(osQueueCount(&queue) == 0 && queue.waiters == NULL);

    // mutex
    TEST_ASSERT(first == second);
    TEST_ASSERT(first == (uint32_t)locked[0] + locked[1] + locked[2]);
    TEST_ASSERT_FAST(locked[0] > 100 && locked[1] > 100 && locked[2] > 100);
    TEST_ASSERT(mutex.owner == NULL && mutex.waiters == NULL);

    // interrupts, none of them had to wait for long
    TEST_ASSERT_FAST(lateness < 2000);
    TEST_ASSERT(given > DURATION / 4);
    TEST_ASSERT(taken == given);
    // flags are not counted: whoever is too slow to take them finds them set, not set twice
    TEST_ASSERT(listened <= flagged / 2);
    TEST_ASSERT_FAST(listened == flagged / 2);

    // Timers. We are woken up on time, but with a tick this fast there is a
    // chance for another one before we manage to ask what time it is.
    TEST_ASSERT_FAST(elapsed == DURATION || elapsed == DURATION + 1);
    TEST_ASSERT(elapsed >= DURATION);
    // (the timer might have been stopped a tick before we asked for the time)
    TEST_ASSERT(periods == elapsed / 7 || periods + 1 == elapsed / 7);
    TEST_ASSERT_FAST(periods == DURATION / 7);
    TEST_ASSERT(shots > 100);

    // the least important task of all had its chances
    TEST_ASSERT_FAST(spins > 1000);

    // with nobody left to keep the processor busy the idle task cleans up
    osTaskDelete(spinner);
    osDelay(5);

#if OS_CFG_DYNAMIC
    // the least important ones do not stand a chance against everybody else
    // without aging, so there is no room for new ones most of the time.
#if OS_CFG_AGING
    TEST_ASSERT_FAST(created > 500);
#else
    TEST_ASSERT_FAST(created > 5);
#endif
    TEST_ASSERT(finished == created);
    TEST_ASSERT(refused == 0);

    void *again = osMalloc(16);
    TEST_ASSERT(again == probe);
    osFree(again);
#endif

#if OS_CFG_STACK_CHECK
    // nobody came close to the end of the stack
    uint16_t least = 0xffff;
    for(uint8_t i = 0; i < everyones; i++) {
        uint16_t unused = osTaskStackFree(everyone[i]);
        if(unused < least)
            least = unused;
    }

    testNumber("stack left (tasks)", least);
    testNumber("stack left (idle)", osTaskStackFree(&osIdleTask));
    TEST_ASSERT(least > 64);
    TEST_ASSERT(osTaskStackFree(NULL) > 64);
    TEST_ASSERT(osTaskStackFree(&osIdleTask) > 64);
#endif

    testPass();
}

int main(void)
{
    osInit();

    osTaskCreate(mainTask, NULL, memoryMain, sizeof(memoryMain), 10);
    osRun();
}
