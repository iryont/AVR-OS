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

#include "system/system.h"

// LED of Arduino Mega 2560 (pin 13)
#define LED_DDR     DDRB
#define LED_PORT    PORTB
#define LED_BIT     PB7

// memory of a task is its stack and the task itself
static uint8_t memory01[OS_TASK_MEMORY(128)];
static uint8_t memory02[OS_TASK_MEMORY(128)];
static uint8_t memory03[OS_TASK_MEMORY(128)];
static uint8_t memory04[OS_TASK_MEMORY(128)];

// lockable object, ready to use as it is
static OsMutex mutex;

// queue of three numbers
static OsQueue queue;
static uint8_t queueMemory[OS_QUEUE_MEMORY(sizeof(uint16_t), 3)];

// both numbers are changed together, so they are equal whenever the mutex is ours
volatile uint16_t a = 0;
volatile uint16_t b = 0;

void task01(void* parameters)
{
    (void)parameters;

    // blinks once a second, no matter how long the others keep the processor busy
    OsTick wake = osTickCount();

    LED_DDR |= (1 << LED_BIT);

    while(1) {
        LED_PORT ^= (1 << LED_BIT);
        osDelayUntil(&wake, OS_MS(500));
    }
}

void task02(void* parameters)
{
    (void)parameters;

    // never sleeps: whatever is left of the processor is taken by this task
    while(1) {
        osMutexLock(&mutex);

        a++;
        b++;

        osMutexUnlock(&mutex);
    }
}

void task03(void* parameters)
{
    (void)parameters;

    // more important than the task above and shares the numbers with it
    while(1) {
        osMutexLock(&mutex);

        a++;
        b++;

        // send the number further every now and then, wait if the queue is full
        uint16_t number = a;
        osMutexUnlock(&mutex);

        osQueueSend(&queue, &number, OS_WAIT_FOREVER);
        osDelay(OS_MS(20));
    }
}

void task04(void* parameters)
{
    (void)parameters;

    // sleeps until there is something in the queue
    while(1) {
        uint16_t number;
        if(osQueueReceive(&queue, &number, OS_MS(100))) {
            // use the number
        } else {
            // nothing for 100 ms
        }
    }
}

int main(void)
{
    // initialize operating system
    osInit();

    osQueueInit(&queue, queueMemory, sizeof(uint16_t), 3);

    // add some tasks
    osTaskCreate(task01, NULL, memory01, sizeof(memory01), 4);
    osTaskCreate(task02, NULL, memory02, sizeof(memory02), 1);
    osTaskCreate(task03, NULL, memory03, sizeof(memory03), 2);
    osTaskCreate(task04, NULL, memory04, sizeof(memory04), 3);

    // run
    osRun();
}
