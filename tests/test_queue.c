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

// Queue: order of messages, blocking on both ends, timeouts, interrupts.

// VARIANT: noaging -DOS_CFG_AGING=0

#include "test.h"

#define TASKS 4
#define MESSAGES 300

typedef struct {
    uint8_t sender;
    uint16_t number;
    uint16_t check;
} Message;

static uint8_t memoryMain[OS_TASK_MEMORY(TEST_STACK)];
static uint8_t memory[TASKS][OS_TASK_MEMORY(TEST_STACK)];
static OsTask *tasks[TASKS];

static OsQueue queue;
static uint8_t queueBuffer[OS_QUEUE_MEMORY(sizeof(Message), 3)];

static OsQueue samples;
static uint8_t samplesBuffer[OS_QUEUE_MEMORY(sizeof(uint16_t), 8)];

static volatile uint16_t received[2];
static volatile uint16_t expected[2];
static volatile uint16_t produced = 0;
static volatile uint16_t dropped = 0;
static volatile uint16_t consumed = 0;

static Message message(uint8_t sender, uint16_t number)
{
    Message message = { sender, number, (uint16_t)(number * 31 + sender) };
    return message;
}

static void check(const Message *message, uint8_t sender, uint16_t number)
{
    TEST_ASSERT(message->sender == sender);
    TEST_ASSERT(message->number == number);
    TEST_ASSERT(message->check == (uint16_t)(number * 31 + sender));
}

static void join(void)
{
    for(uint8_t i = 0; i < TASKS; i++) {
        while(tasks[i] && osTaskGetState(tasks[i]) != OS_TASK_DEAD)
            osDelay(1);

        tasks[i] = NULL;
    }
}

static void producer(void *param)
{
    uint8_t id = (uint8_t)(uintptr_t)param;

    for(uint16_t i = 0; i < MESSAGES; i++) {
        Message sent = message(id, i);
        TEST_ASSERT(osQueueSend(&queue, &sent, OS_WAIT_FOREVER));

        if(i % 16 == id)
            osDelay(1);
    }
}

static void consumer(void *param)
{
    (void)param;

    // messages of each producer come in the order they have been sent
    for(uint16_t i = 0; i < 2 * MESSAGES; i++) {
        Message got;
        TEST_ASSERT(osQueueReceive(&queue, &got, OS_WAIT_FOREVER));
        TEST_ASSERT(got.sender < 2);
        check(&got, got.sender, expected[got.sender]);

        expected[got.sender]++;
        received[got.sender]++;

        if(i % 50 == 0)
            osDelay(2);
    }
}

static void late(void *param)
{
    (void)param;

    Message sent = message(9, 99);

    osDelay(5);
    TEST_ASSERT(osQueueSend(&queue, &sent, OS_WAIT_FOREVER));
}

static void greedy(void *param)
{
    (void)param;

    Message got;

    osDelay(5);
    TEST_ASSERT(osQueueReceive(&queue, &got, OS_WAIT_FOREVER));
    check(&got, 1, 0);
}

OS_ISR(TIMER1_COMPA_vect)
{
    uint16_t sample = produced;

    if(osQueueSendFromISR(&samples, &sample))
        produced++;
    else
        dropped++;
}

static void sampler(void *param)
{
    (void)param;

    // whatever made it to the queue comes out in order
    while(1) {
        uint16_t sample;
        TEST_ASSERT(osQueueReceive(&samples, &sample, OS_WAIT_FOREVER));
        TEST_ASSERT(sample == consumed);
        consumed++;
    }
}

static void mainTask(void *param)
{
    (void)param;

    Message got;
    Message sent;

    osQueueInit(&queue, queueBuffer, sizeof(Message), 3);

    // empty
    TEST_ASSERT(osQueueCount(&queue) == 0);
    TEST_ASSERT(!osQueueReceive(&queue, &got, OS_NO_WAIT));

    OsTick start = osTickCount();
    TEST_ASSERT(!osQueueReceive(&queue, &got, 4));
    TEST_ASSERT((OsTick)(osTickCount() - start) == 4);

    // full
    for(uint8_t i = 0; i < 3; i++) {
        sent = message(1, i);
        TEST_ASSERT(osQueueSend(&queue, &sent, OS_NO_WAIT));
    }

    TEST_ASSERT(osQueueCount(&queue) == 3);
    sent = message(1, 3);
    TEST_ASSERT(!osQueueSend(&queue, &sent, OS_NO_WAIT));

    start = osTickCount();
    TEST_ASSERT(!osQueueSend(&queue, &sent, 6));
    TEST_ASSERT((OsTick)(osTickCount() - start) == 6);
    TEST_ASSERT(queue.waiters == NULL);

    // first in, first out - over and over again, so the buffer wraps around
    for(uint16_t i = 3; i < 50; i++) {
        TEST_ASSERT(osQueueReceive(&queue, &got, OS_NO_WAIT));
        check(&got, 1, i - 3);

        sent = message(1, i);
        TEST_ASSERT(osQueueSend(&queue, &sent, OS_NO_WAIT));
    }

    for(uint16_t i = 47; i < 50; i++) {
        TEST_ASSERT(osQueueReceive(&queue, &got, OS_NO_WAIT));
        check(&got, 1, i);
    }

    TEST_ASSERT(osQueueCount(&queue) == 0);

    // message sent while we wait for it
    tasks[0] = osTaskCreate(late, NULL, memory[0], sizeof(memory[0]), 1);
    start = osTickCount();
    TEST_ASSERT(osQueueReceive(&queue, &got, 100));
    TEST_ASSERT((OsTick)(osTickCount() - start) == 5);
    check(&got, 9, 99);

    // it went straight to us
    TEST_ASSERT(osQueueCount(&queue) == 0);
    join();

    // message received while we wait for a room in the queue
    for(uint8_t i = 0; i < 3; i++) {
        sent = message(1, i);
        TEST_ASSERT(osQueueSend(&queue, &sent, OS_NO_WAIT));
    }

    tasks[0] = osTaskCreate(greedy, NULL, memory[0], sizeof(memory[0]), 1);
    sent = message(1, 3);
    start = osTickCount();
    TEST_ASSERT(osQueueSend(&queue, &sent, 100));
    TEST_ASSERT((OsTick)(osTickCount() - start) == 5);

    // our message took the place of the one which is gone, right behind the others
    TEST_ASSERT(osQueueCount(&queue) == 3);
    join();

    for(uint8_t i = 1; i <= 3; i++) {
        TEST_ASSERT(osQueueReceive(&queue, &got, OS_NO_WAIT));
        check(&got, 1, i);
    }

    // two producers and a consumer, slower or faster than each other from time to time
    tasks[0] = osTaskCreate(producer, (void*)0, memory[0], sizeof(memory[0]), 3);
    tasks[1] = osTaskCreate(producer, (void*)1, memory[1], sizeof(memory[1]), 5);
    tasks[2] = osTaskCreate(consumer, NULL, memory[2], sizeof(memory[2]), 4);
    join();

    TEST_ASSERT(received[0] == MESSAGES && received[1] == MESSAGES);
    TEST_ASSERT(osQueueCount(&queue) == 0 && queue.waiters == NULL);

    // interrupt as a producer
    osQueueInit(&samples, samplesBuffer, sizeof(uint16_t), 8);
    tasks[0] = osTaskCreate(sampler, NULL, memory[0], sizeof(memory[0]), 20);
    testInterruptStart(1913);
    osDelay(300);

    // we do not let the consumer run for a while, so the queue fills up
    osTaskSetPriority(NULL, 30);
    testWork(3);
    osTaskSetPriority(NULL, 10);

    osDelay(300);
    testInterruptStop();
    osDelay(1);

    testNumber("produced", produced);
    testNumber("dropped", dropped);

    TEST_ASSERT(produced > 1000);
    TEST_ASSERT(dropped > 0);
    TEST_ASSERT(consumed == produced);
    osTaskDelete(tasks[0]);

    // interrupt handlers may receive as well
    sent = message(2, 7);
    TEST_ASSERT(osQueueSend(&queue, &sent, OS_NO_WAIT));
    OS_CRITICAL {
        TEST_ASSERT(osQueueReceiveFromISR(&queue, &got));
        TEST_ASSERT(!osQueueReceiveFromISR(&queue, &got));
    }
    check(&got, 2, 7);

#if OS_CFG_DYNAMIC
    OsQueue *dynamic = osQueueCreate(sizeof(uint32_t), 4);
    TEST_ASSERT(dynamic != NULL);

    for(uint32_t i = 0; i < 4; i++) {
        uint32_t value = 0x11223344 * (i + 1);
        TEST_ASSERT(osQueueSend(dynamic, &value, OS_NO_WAIT));
    }

    for(uint32_t i = 0; i < 4; i++) {
        uint32_t value;
        TEST_ASSERT(osQueueReceive(dynamic, &value, OS_NO_WAIT));
        TEST_ASSERT(value == 0x11223344 * (i + 1));
    }

    osQueueDestroy(dynamic);
#endif

    testPass();
}

int main(void)
{
    osInit();

    osTaskCreate(mainTask, NULL, memoryMain, sizeof(memoryMain), 10);
    osRun();
}
