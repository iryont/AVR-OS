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

#ifndef OS_TIMER_H
#define OS_TIMER_H

#include "scheduler.h"

#ifdef __cplusplus
extern "C" {
#endif

#if OS_CFG_TIMERS

typedef struct OsTimer OsTimer;

struct OsTimer {
    OsTimer *next;
    OsTick when;
    OsTick period;
    void (*function)(void*);
    void *param;
};

// calls the function after the given number of ticks and then every period
// ticks, or just once if the period is zero. The function is called by the
// tick interrupt: it has to be short, it cannot block and it is limited to
// functions with the FromISR suffix. Starting a timer which is running
// already starts it over.
void osTimerStart(OsTimer *timer, OsTick delay, OsTick period, void (*function)(void*), void *param);
void osTimerStop(OsTimer *timer);
bool osTimerActive(OsTimer *timer);

// internals of the kernel
extern OsTimer *osTimers;
void osTimerTick(OsTick ticks);

#endif

#ifdef __cplusplus
}
#endif

#endif
