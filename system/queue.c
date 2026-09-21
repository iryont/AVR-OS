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

#include <string.h>
#include "queue.h"
#include "memory.h"

// tasks never race for a message or for a room in the queue once woken up:
// whoever wakes a task does its job first. A sender copies the message
// straight to the receiver waiting for it, a receiver which makes a room
// in the queue fills it with the message of the sender waiting for it.

void osQueueInit(OsQueue *queue, void *buffer, uint8_t itemSize, uint8_t length)
{
    queue->waiters = NULL;
    queue->buffer = (uint8_t*)buffer;
    queue->itemSize = itemSize;
    queue->length = length;
    queue->count = 0;
    queue->head = 0;
}

static void osQueueAppend(OsQueue *queue, const void *item)
{
    uint16_t tail = (uint16_t)queue->head + queue->count;
    if(tail >= queue->length)
        tail -= queue->length;

    memcpy(queue->buffer + tail * queue->itemSize, item, queue->itemSize);
    queue->count++;
}

static bool osQueuePut(OsQueue *queue, const void *item, OsTick timeout)
{
    // queue is empty, so whoever waits for it waits for a message
    OsTask *waiter = queue->waiters;
    if(waiter && !queue->count) {
        memcpy(waiter->wait.item, item, queue->itemSize);
        osWake(waiter, true);
        return true;
    }

    if(queue->count < queue->length) {
        osQueueAppend(queue, item);
        return true;
    }

    if(timeout == OS_NO_WAIT)
        return false;

    // message is taken from us by a receiver
    osCurrentTask->wait.item = (void*)item;
    return osBlock(&queue->waiters, timeout);
}

static bool osQueueGet(OsQueue *queue, void *item, OsTick timeout)
{
    if(queue->count) {
        memcpy(item, queue->buffer + (uint16_t)queue->head * queue->itemSize, queue->itemSize);

        if(++queue->head == queue->length)
            queue->head = 0;

        queue->count--;

        // queue was full if somebody waits for it, now there is a room for one more
        OsTask *waiter = queue->waiters;
        if(waiter) {
            osQueueAppend(queue, waiter->wait.item);
            osWake(waiter, true);
        }

        return true;
    }

    if(timeout == OS_NO_WAIT)
        return false;

    // message is delivered by a sender
    osCurrentTask->wait.item = item;
    return osBlock(&queue->waiters, timeout);
}

bool osQueueSend(OsQueue *queue, const void *item, OsTick timeout)
{
    bool sent;

    OS_CRITICAL {
        sent = osQueuePut(queue, item, timeout);
        osSchedule(0);
    }

    return sent;
}

bool osQueueSendFromISR(OsQueue *queue, const void *item)
{
    return osQueuePut(queue, item, OS_NO_WAIT);
}

bool osQueueReceive(OsQueue *queue, void *item, OsTick timeout)
{
    bool received;

    OS_CRITICAL {
        received = osQueueGet(queue, item, timeout);
        osSchedule(0);
    }

    return received;
}

bool osQueueReceiveFromISR(OsQueue *queue, void *item)
{
    return osQueueGet(queue, item, OS_NO_WAIT);
}

uint8_t osQueueCount(OsQueue *queue)
{
    return queue->count;
}

#if OS_CFG_DYNAMIC
OsQueue *osQueueCreate(uint8_t itemSize, uint8_t length)
{
    // buffer follows the queue itself
    OsQueue *queue = (OsQueue*)osMalloc(sizeof(OsQueue) + OS_QUEUE_MEMORY(itemSize, length));
    if(queue)
        osQueueInit(queue, queue + 1, itemSize, length);

    return queue;
}

void osQueueDestroy(OsQueue *queue)
{
    osFree(queue);
}
#endif
