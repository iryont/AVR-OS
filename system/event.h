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

#ifndef OS_EVENT_H
#define OS_EVENT_H

#include "scheduler.h"

#ifdef __cplusplus
extern "C" {
#endif

// eight flags tasks can wait for, an event filled with zeros is a valid one
typedef struct {
    OsTask *waiters;
    uint8_t flags;
} OsEvent;

// wait for any of the flags (default) or for all of them
#define OS_EVENT_ANY    0x00
#define OS_EVENT_ALL    0x01

// flags the task has been waiting for are cleared once it gets them, unless asked not to
#define OS_EVENT_KEEP   0x02

void osEventInit(OsEvent *event);

// returns the flags which ended the wait, zero means timeout
uint8_t osEventWait(OsEvent *event, uint8_t flags, uint8_t options, OsTick timeout);

void osEventSet(OsEvent *event, uint8_t flags);
void osEventSetFromISR(OsEvent *event, uint8_t flags);
void osEventClear(OsEvent *event, uint8_t flags);
uint8_t osEventGet(OsEvent *event);

#if OS_CFG_DYNAMIC
OsEvent *osEventCreate(void);
void osEventDestroy(OsEvent *event);
#endif

#ifdef __cplusplus
}
#endif

#endif
