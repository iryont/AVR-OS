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

#include "timer.h"

#if OS_CFG_TIMERS

// running timers, the one which expires first at the front
OsTimer *osTimers = NULL;

static void osTimerInsert(OsTimer *timer, OsTick now, OsTick delay)
{
    // compare the time left rather than the time itself, tick counter overflows
    OsTimer **list = &osTimers;
    OsTimer *it;
    while((it = *list) != NULL && (OsTick)(it->when - now) <= delay)
        list = &it->next;

    timer->when = now + delay;
    timer->next = it;
    *list = timer;
}

static bool osTimerRemove(OsTimer *timer)
{
    OsTimer **list = &osTimers;
    OsTimer *it;
    while((it = *list) != NULL) {
        if(it == timer) {
            *list = timer->next;
            return true;
        }

        list = &it->next;
    }

    return false;
}

void osTimerStart(OsTimer *timer, OsTick delay, OsTick period, void (*function)(void*), void *param)
{
    // the current tick is gone already, the next one is the earliest we can get
    if(!delay)
        delay = 1;

    OS_CRITICAL {
        osTimerRemove(timer);

        timer->period = period;
        timer->function = function;
        timer->param = param;

        osTimerInsert(timer, osTickCount(), delay);
    }
}

void osTimerStop(OsTimer *timer)
{
    OS_CRITICAL {
        osTimerRemove(timer);
    }
}

bool osTimerActive(OsTimer *timer)
{
    bool active = false;

    OS_CRITICAL {
        for(OsTimer *it = osTimers; it; it = it->next) {
            if(it == timer) {
                active = true;
                break;
            }
        }
    }

    return active;
}

void osTimerTick(OsTick ticks)
{
    // timer is back on the list before its function is called,
    // so the function is free to stop it or to start it over
    OsTimer *timer;
    while((timer = osTimers) != NULL && timer->when == ticks) {
        osTimers = timer->next;

        if(timer->period)
            osTimerInsert(timer, ticks, timer->period);

        timer->function(timer->param);
    }
}

#endif
