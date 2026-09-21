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

#ifndef OS_SCHEDULER_H
#define OS_SCHEDULER_H

#include "tasks.h"

#ifdef __cplusplus
extern "C" {
#endif

// timeouts
#define OS_NO_WAIT      ((OsTick)0)
#define OS_WAIT_FOREVER ((OsTick)-1)

// milliseconds to ticks, rounded up
#define OS_MS(ms) ((OsTick)(((ms) * (uint32_t)OS_CFG_TICK_HZ + 999) / 1000))

// an interrupt handler which wakes tasks up. Tasks woken by the handler get
// the processor as soon as it returns, not with the next tick. Only functions
// with the FromISR suffix (and those which never block) can be called inside.
#define OS_ISR(vector)                                                         \
    static inline void vector##_handler(void) __attribute__ ((always_inline)); \
    ISR(vector)                                                                \
    {                                                                          \
        vector##_handler();                                                    \
        osSchedule(0);                                                         \
    }                                                                          \
    static inline void vector##_handler(void)

extern OsTask *osCurrentTask;
extern OsTask *osReadyList;
extern OsTask osIdleTask;

OsTick osTickCount(void);
void osYield(void);
void osDelay(OsTick ticks);
void osDelayUntil(OsTick *previous, OsTick period);

// internals of the kernel, interrupts have to be disabled
void osListInsert(OsTask **list, OsTask *task, uint8_t key);
void osListRemove(OsTask **list, OsTask *task);
void osRequeue(OsTask *task);
uint8_t osInheritedPriority(OsTask *task);
void osPriorityRaise(OsTask *task, uint8_t priority);

uint8_t osBlock(OsTask **list, OsTick timeout);
void osUnblock(OsTask *task);
void osWake(OsTask *task, uint8_t result);
void osReady(OsTask *task);
uint8_t osDispatch(void);
void osSchedule(uint8_t yield);
void osTickHandler(void);

#ifdef __cplusplus
}
#endif

#endif
