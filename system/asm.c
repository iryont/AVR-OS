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

#include "asm.h"
#include "scheduler.h"

#ifndef F_CPU
#error "F_CPU is not defined, the tick cannot be derived without it (e.g. -DF_CPU=16000000UL)"
#endif

// registers of the timer selected by OS_CFG_TICK_TIMER
#define OS_CONCAT_(a, b, c) a##b##c
#define OS_CONCAT(a, b, c) OS_CONCAT_(a, b, c)
#define OS_TIMER(prefix, suffix) OS_CONCAT(prefix, OS_CFG_TICK_TIMER, suffix)

#if OS_CFG_TICK_TIMER == 0 || OS_CFG_TICK_TIMER == 2
#define OS_TIMER_RANGE 256
#else
#define OS_TIMER_RANGE 65536
#endif

// timer counts per tick for the given prescaler, rounded to the nearest
#define OS_TICK_COUNTS(prescaler) \
    ((F_CPU + (prescaler) * 1UL * OS_CFG_TICK_HZ / 2) / ((prescaler) * 1UL * OS_CFG_TICK_HZ))

// the smallest prescaler which fits gives the most accurate tick
#if OS_CFG_TICK_TIMER == 2
// timer 2 offers more prescalers than the other ones
#if OS_TICK_COUNTS(1) <= OS_TIMER_RANGE
#define OS_TICK_PRESCALER 1
#define OS_TICK_CLOCK 1
#elif OS_TICK_COUNTS(8) <= OS_TIMER_RANGE
#define OS_TICK_PRESCALER 8
#define OS_TICK_CLOCK 2
#elif OS_TICK_COUNTS(32) <= OS_TIMER_RANGE
#define OS_TICK_PRESCALER 32
#define OS_TICK_CLOCK 3
#elif OS_TICK_COUNTS(64) <= OS_TIMER_RANGE
#define OS_TICK_PRESCALER 64
#define OS_TICK_CLOCK 4
#elif OS_TICK_COUNTS(128) <= OS_TIMER_RANGE
#define OS_TICK_PRESCALER 128
#define OS_TICK_CLOCK 5
#elif OS_TICK_COUNTS(256) <= OS_TIMER_RANGE
#define OS_TICK_PRESCALER 256
#define OS_TICK_CLOCK 6
#elif OS_TICK_COUNTS(1024) <= OS_TIMER_RANGE
#define OS_TICK_PRESCALER 1024
#define OS_TICK_CLOCK 7
#endif
#else
#if OS_TICK_COUNTS(1) <= OS_TIMER_RANGE
#define OS_TICK_PRESCALER 1
#define OS_TICK_CLOCK 1
#elif OS_TICK_COUNTS(8) <= OS_TIMER_RANGE
#define OS_TICK_PRESCALER 8
#define OS_TICK_CLOCK 2
#elif OS_TICK_COUNTS(64) <= OS_TIMER_RANGE
#define OS_TICK_PRESCALER 64
#define OS_TICK_CLOCK 3
#elif OS_TICK_COUNTS(256) <= OS_TIMER_RANGE
#define OS_TICK_PRESCALER 256
#define OS_TICK_CLOCK 4
#elif OS_TICK_COUNTS(1024) <= OS_TIMER_RANGE
#define OS_TICK_PRESCALER 1024
#define OS_TICK_CLOCK 5
#endif
#endif

#ifndef OS_TICK_PRESCALER
#error "OS_CFG_TICK_HZ is too low for the selected timer, pick a 16-bit timer or a faster tick"
#endif

#if OS_TICK_COUNTS(OS_TICK_PRESCALER) < 2
#error "OS_CFG_TICK_HZ is too high for this F_CPU"
#endif

// the tick is as close to OS_CFG_TICK_HZ as the timer gets, which is not always what was asked for
#if OS_TICK_COUNTS(OS_TICK_PRESCALER) * OS_TICK_PRESCALER * OS_CFG_TICK_HZ != F_CPU
#if OS_TIMER_RANGE == 256
#pragma message "the tick is not exactly OS_CFG_TICK_HZ with this clock, a 16-bit timer (OS_CFG_TICK_TIMER) gets closer"
#else
#pragma message "the tick is not exactly OS_CFG_TICK_HZ with this clock"
#endif
#endif

// members of the task the context switch deals with
_Static_assert(offsetof(OsTask, sp) == 0, "sp has to be the first member of OsTask");
_Static_assert(offsetof(OsTask, result) == 2, "result has to follow sp in OsTask");

void osPortTickStart(void)
{
    // clear timer on compare match, the clock stays stopped for now
#if OS_TIMER_RANGE == 256
    OS_TIMER(TCCR, A) = (1 << OS_TIMER(WGM, 1));
    OS_TIMER(TCCR, B) = 0;
#else
    OS_TIMER(TCCR, A) = 0;
    OS_TIMER(TCCR, B) = (1 << OS_TIMER(WGM, 2));
#endif

    OS_TIMER(OCR, A) = OS_TICK_COUNTS(OS_TICK_PRESCALER) - 1;
    OS_TIMER(TCNT, ) = 0;

    // enable the compare match interrupt, drop whatever was pending. The timer
    // is ours, so are its other interrupts: they might have been left enabled
    // by a bootloader and there is nobody to handle them.
    OS_TIMER(TIFR, ) = (1 << OS_TIMER(OCF, A));
    OS_TIMER(TIMSK, ) = (1 << OS_TIMER(OCIE, A));

    // run
    OS_TIMER(TCCR, B) |= OS_TICK_CLOCK;
}

static uint8_t *osPortPushAddress(uint8_t *sp, void (*address)(void))
{
    // addresses of functions always fit in 16 bits, above that the linker
    // makes them point to a stub located in the lower part of the memory
    uint16_t word = (uint16_t)address;

    *sp-- = (uint8_t)(word >> 0);
    *sp-- = (uint8_t)(word >> 8);
#if OS_PC_SIZE == 3
    *sp-- = 0;
#endif

    return sp;
}

// every task starts here: move the parameter to where the function expects
// its first argument, enable interrupts and jump to the function itself
static void osPortTaskStart(void) __attribute__ ((naked, used));
static void osPortTaskStart(void)
{
    asm volatile (
        "movw r24, r2                   \n\t"
        "sei                            \n\t"
        "ret                            \n\t"
    );
}

uint8_t *osPortStackInit(uint8_t *top, void (*function)(void*), void *param)
{
    // stack of a new task looks like the task has been switched out by
    // osPortSwitch, addresses are taken from the stack one by one using ret:
    // osPortSwitch -> osPortTaskStart -> function -> osTaskExit
    top = osPortPushAddress(top, (void (*)(void))osTaskExit);
    top = osPortPushAddress(top, (void (*)(void))function);
    top = osPortPushAddress(top, osPortTaskStart);

    // R2-R3
    // parameter of the task, expected there by osPortTaskStart
    *top-- = (uint8_t)((uint16_t)param >> 0);
    *top-- = (uint8_t)((uint16_t)param >> 8);

    // R4-R17, R28-R29
    for(uint8_t i = 0; i < 16; i++)
        *top-- = 0x00;

    return top;
}

// Only the registers a function has to preserve are saved (R2-R17, R28-R29),
// the compiler already assumes the remaining ones are lost after any call.
// when a task gets preempted those are saved as well, by the interrupt
// handler which ends up here - like every handler which calls a function.
// state of interrupts is not a part of the context: they are disabled on
// entry, they are still disabled on return and it is the caller who brings
// them back, either by leaving its critical section or by reti.
// the value returned to the task which is back is the result it has been
// woken up with. It saves the callers from keeping anything on the stack
// just to find that out: blocking is a chain of jumps which ends up here.
uint8_t osPortSwitch(OsTask *next __attribute__ ((unused)), OsTask *prev __attribute__ ((unused))) __attribute__ ((naked, noinline));
uint8_t osPortSwitch(OsTask *next __attribute__ ((unused)), OsTask *prev __attribute__ ((unused)))
{
    asm volatile (
        "push  r2                       \n\t"
        "push  r3                       \n\t"
        "push  r4                       \n\t"
        "push  r5                       \n\t"
        "push  r6                       \n\t"
        "push  r7                       \n\t"
        "push  r8                       \n\t"
        "push  r9                       \n\t"
        "push  r10                      \n\t"
        "push  r11                      \n\t"
        "push  r12                      \n\t"
        "push  r13                      \n\t"
        "push  r14                      \n\t"
        "push  r15                      \n\t"
        "push  r16                      \n\t"
        "push  r17                      \n\t"
        "push  r28                      \n\t"
        "push  r29                      \n\t"
        "movw  r30, r22                 \n\t"
        "in    r0, __SP_L__             \n\t"
        "std   z+0, r0                  \n\t"
        "in    r0, __SP_H__             \n\t"
        "std   z+1, r0                  \n\t"
        "movw  r30, r24                 \n\t"
        "ldd   r0, z+0                  \n\t"
        "out   __SP_L__, r0             \n\t"
        "ldd   r0, z+1                  \n\t"
        "out   __SP_H__, r0             \n\t"
        "ldd   r24, z+2                 \n\t"
        "pop   r29                      \n\t"
        "pop   r28                      \n\t"
        "pop   r17                      \n\t"
        "pop   r16                      \n\t"
        "pop   r15                      \n\t"
        "pop   r14                      \n\t"
        "pop   r13                      \n\t"
        "pop   r12                      \n\t"
        "pop   r11                      \n\t"
        "pop   r10                      \n\t"
        "pop   r9                       \n\t"
        "pop   r8                       \n\t"
        "pop   r7                       \n\t"
        "pop   r6                       \n\t"
        "pop   r5                       \n\t"
        "pop   r4                       \n\t"
        "pop   r3                       \n\t"
        "pop   r2                       \n\t"
        "ret                            \n\t"
    );
}

ISR(OS_TIMER(TIMER, _COMPA_vect))
{
    osTickHandler();
}
