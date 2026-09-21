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

#include "event.h"
#include "memory.h"

void osEventInit(OsEvent *event)
{
    event->waiters = NULL;
    event->flags = 0;
}

// flags which satisfy the request, zero if there is nothing to be satisfied with yet
static uint8_t osEventMatch(uint8_t flags, uint8_t wanted, uint8_t options)
{
    flags &= wanted;

    if((options & OS_EVENT_ALL) && flags != wanted)
        return 0;

    return flags;
}

uint8_t osEventWait(OsEvent *event, uint8_t flags, uint8_t options, OsTick timeout)
{
    uint8_t matched;

    OS_CRITICAL {
        matched = osEventMatch(event->flags, flags, options);

        if(matched) {
            if(!(options & OS_EVENT_KEEP))
                event->flags &= ~matched;
        } else if(timeout != OS_NO_WAIT) {
            OsTask *task = osCurrentTask;

            task->wait.event.flags = flags;
            task->wait.event.options = options;

            // flags we were waiting for are replaced with the flags we got
            if(osBlock(&event->waiters, timeout))
                matched = task->wait.event.flags;
        }
    }

    return matched;
}

void osEventSetFromISR(OsEvent *event, uint8_t flags)
{
    event->flags |= flags;

    // the most important tasks are served first, flags taken by them are gone
    OsTask *task = event->waiters;
    while(task) {
        OsTask *next = task->next;

        uint8_t options = task->wait.event.options;
        uint8_t matched = osEventMatch(event->flags, task->wait.event.flags, options);

        if(matched) {
            if(!(options & OS_EVENT_KEEP))
                event->flags &= ~matched;

            task->wait.event.flags = matched;
            osWake(task, true);
        }

        task = next;
    }
}

void osEventSet(OsEvent *event, uint8_t flags)
{
    OS_CRITICAL {
        osEventSetFromISR(event, flags);
        osSchedule(0);
    }
}

void osEventClear(OsEvent *event, uint8_t flags)
{
    OS_CRITICAL {
        event->flags &= ~flags;
    }
}

uint8_t osEventGet(OsEvent *event)
{
    return event->flags;
}

#if OS_CFG_DYNAMIC
OsEvent *osEventCreate(void)
{
    OsEvent *event = (OsEvent*)osMalloc(sizeof(OsEvent));
    if(event)
        osEventInit(event);

    return event;
}

void osEventDestroy(OsEvent *event)
{
    osFree(event);
}
#endif
