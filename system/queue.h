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

#ifndef OS_QUEUE_H
#define OS_QUEUE_H

#include "scheduler.h"

#ifdef __cplusplus
extern "C" {
#endif

// queue of messages, every message is a copy of itemSize bytes
typedef struct {
    // senders if the queue is full, receivers if it is empty
    OsTask *waiters;

    uint8_t *buffer;
    uint8_t itemSize;
    uint8_t length;
    uint8_t count;
    uint8_t head;
} OsQueue;

// size of the buffer needed by a queue
#define OS_QUEUE_MEMORY(itemSize, length) ((uint16_t)(itemSize) * (length))

void osQueueInit(OsQueue *queue, void *buffer, uint8_t itemSize, uint8_t length);
bool osQueueSend(OsQueue *queue, const void *item, OsTick timeout);
bool osQueueSendFromISR(OsQueue *queue, const void *item);
bool osQueueReceive(OsQueue *queue, void *item, OsTick timeout);
bool osQueueReceiveFromISR(OsQueue *queue, void *item);
uint8_t osQueueCount(OsQueue *queue);

#if OS_CFG_DYNAMIC
OsQueue *osQueueCreate(uint8_t itemSize, uint8_t length);
void osQueueDestroy(OsQueue *queue);
#endif

#ifdef __cplusplus
}
#endif

#endif
