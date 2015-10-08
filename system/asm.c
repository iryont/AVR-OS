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

#include <avr/interrupt.h>
#include "asm.h"
#include "scheduler.h"

// save context of the current task (osTask)
#define SAVE_CONTEXT                       \
  asm volatile (                           \
    "push  r0                       \n\t"  \
    "in    r0, __SREG__             \n\t"  \
    "cli                            \n\t"  \
    "push  r0                       \n\t"  \
    "push  r1                       \n\t"  \
    "clr  r1                        \n\t"  \
    "push  r2                       \n\t"  \
    "push  r3                       \n\t"  \
    "push  r4                       \n\t"  \
    "push  r5                       \n\t"  \
    "push  r6                       \n\t"  \
    "push  r7                       \n\t"  \
    "push  r8                       \n\t"  \
    "push  r9                       \n\t"  \
    "push  r10                      \n\t"  \
    "push  r11                      \n\t"  \
    "push  r12                      \n\t"  \
    "push  r13                      \n\t"  \
    "push  r14                      \n\t"  \
    "push  r15                      \n\t"  \
    "push  r16                      \n\t"  \
    "push  r17                      \n\t"  \
    "push  r18                      \n\t"  \
    "push  r19                      \n\t"  \
    "push  r20                      \n\t"  \
    "push  r21                      \n\t"  \
    "push  r22                      \n\t"  \
    "push  r23                      \n\t"  \
    "push  r24                      \n\t"  \
    "push  r25                      \n\t"  \
    "push  r26                      \n\t"  \
    "push  r27                      \n\t"  \
    "push  r28                      \n\t"  \
    "push  r29                      \n\t"  \
    "push  r30                      \n\t"  \
    "push  r31                      \n\t"  \
    "lds  r26, osCurrentTask        \n\t"  \
    "lds  r27, osCurrentTask + 1    \n\t"  \
    "in    r0, __SP_L__             \n\t"  \
    "st    x+, r0                   \n\t"  \
    "in    r0, __SP_H__             \n\t"  \
    "st    x+, r0                   \n\t"  \
  );


// restore context of the current task (osCurrentTask)
#define RESTORE_CONTEXT                    \
  asm volatile (                           \
    "lds  r26, osCurrentTask        \n\t"  \
    "lds  r27, osCurrentTask + 1    \n\t"  \
    "ld    r28, x+                  \n\t"  \
    "out  __SP_L__, r28             \n\t"  \
    "ld    r29, x+                  \n\t"  \
    "out  __SP_H__, r29             \n\t"  \
    "pop  r31                       \n\t"  \
    "pop  r30                       \n\t"  \
    "pop  r29                       \n\t"  \
    "pop  r28                       \n\t"  \
    "pop  r27                       \n\t"  \
    "pop  r26                       \n\t"  \
    "pop  r25                       \n\t"  \
    "pop  r24                       \n\t"  \
    "pop  r23                       \n\t"  \
    "pop  r22                       \n\t"  \
    "pop  r21                       \n\t"  \
    "pop  r20                       \n\t"  \
    "pop  r19                       \n\t"  \
    "pop  r18                       \n\t"  \
    "pop  r17                       \n\t"  \
    "pop  r16                       \n\t"  \
    "pop  r15                       \n\t"  \
    "pop  r14                       \n\t"  \
    "pop  r13                       \n\t"  \
    "pop  r12                       \n\t"  \
    "pop  r11                       \n\t"  \
    "pop  r10                       \n\t"  \
    "pop  r9                        \n\t"  \
    "pop  r8                        \n\t"  \
    "pop  r7                        \n\t"  \
    "pop  r6                        \n\t"  \
    "pop  r5                        \n\t"  \
    "pop  r4                        \n\t"  \
    "pop  r3                        \n\t"  \
    "pop  r2                        \n\t"  \
    "pop  r1                        \n\t"  \
    "pop  r0                        \n\t"  \
    "out  __SREG__, r0              \n\t"  \
    "pop  r0                        \n\t"  \
  );

void osSetupTimerInterrupt()
{
    #define CTC_MATCH_OVERFLOW  ((F_CPU / 1000) / 8)

    OCR1AH = (uint8_t)(CTC_MATCH_OVERFLOW >> 8);
    OCR1AL = (uint8_t)(CTC_MATCH_OVERFLOW);

    // CTC mode, clock/8
    TCCR1B |= (1 << WGM12) | (1 << CS11);

    // initialize the counter
    TCNT1 = 0;

    // enable the compare match interrupt
    TIMSK |= (1 << OCIE1A);
}

uint8_t osTAS(uint8_t *v)
{
    uint8_t p = 0;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        if((p = *v) == 0)
            *v = 1;
    }

    return p;
}

uint8_t osCAS(uint8_t *v, uint8_t p, uint8_t q)
{
    uint8_t n = 0;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        if((n = *v) == p)
            *v = q;
    }

    return n;
}

uint8_t* osInitializeStack(uint8_t* topOfStack, void (*taskFunction)(void*), void* taskParameter)
{
    uint16_t address = 0;

    // indicates top of the stack - debug information
    *topOfStack = 0x13;
    topOfStack--;
    *topOfStack = 0x37;
    topOfStack--;

    // initialize stack so the task can be started by osTaskSwitchContext
    // task is not running, so we need to put some random values into the context

    // put osTaskExit on stack so when task exits by ret(i) instruction it will go "back" to that function
    address = (uint16_t)osTaskExit;
    *topOfStack = (address >> 0) & 0xff;
    topOfStack--;
    *topOfStack = (address >> 8) & 0xff;
    topOfStack--;

    // put start of the stack on top since it will be fetched by ret(i) instruction of the context switch
    address = (uint16_t)taskFunction;
    *topOfStack = (address >> 0) & 0xff;
    topOfStack--;
    *topOfStack = (address >> 8) & 0xff;
    topOfStack--;

    // R0 and SREG (0x80 - interrupts enabled) registers
    *topOfStack = 0x00;
    topOfStack--;
    *topOfStack = 0x80;
    topOfStack--;

    // initialize registers R1-R23
    // R1 is expected to be zero
    *topOfStack = 0x00;
    topOfStack--;
    *topOfStack = 0x02;
    topOfStack--;
    *topOfStack = 0x03;
    topOfStack--;
    *topOfStack = 0x04;
    topOfStack--;
    *topOfStack = 0x05;
    topOfStack--;
    *topOfStack = 0x06;
    topOfStack--;
    *topOfStack = 0x07;
    topOfStack--;
    *topOfStack = 0x08;
    topOfStack--;
    *topOfStack = 0x09;
    topOfStack--;
    *topOfStack = 0x10;
    topOfStack--;
    *topOfStack = 0x11;
    topOfStack--;
    *topOfStack = 0x12;
    topOfStack--;
    *topOfStack = 0x13;
    topOfStack--;
    *topOfStack = 0x14;
    topOfStack--;
    *topOfStack = 0x15;
    topOfStack--;
    *topOfStack = 0x16;
    topOfStack--;
    *topOfStack = 0x17;
    topOfStack--;
    *topOfStack = 0x18;
    topOfStack--;
    *topOfStack = 0x19;
    topOfStack--;
    *topOfStack = 0x20;
    topOfStack--;
    *topOfStack = 0x21;
    topOfStack--;
    *topOfStack = 0x22;
    topOfStack--;
    *topOfStack = 0x23;
    topOfStack--;

    // initialize R24-R25
    // which is a function argument
    address = (uint16_t)taskParameter;
    *topOfStack = (address >> 0) & 0xff;
    topOfStack--;
    *topOfStack = (address >> 8) & 0xff;
    topOfStack--;

    // initialize R26-R31
    *topOfStack = 0x26;
    topOfStack--;
    *topOfStack = 0x27;
    topOfStack--;
    *topOfStack = 0x28;
    topOfStack--;
    *topOfStack = 0x29;
    topOfStack--;
    *topOfStack = 0x30;
    topOfStack--;
    *topOfStack = 0x31;
    topOfStack--;

    return topOfStack;
}

void osNonSavableYield(void) __attribute__ ((naked));
void osNonSavableYield(void)
{
    // do not save context since there is no "current" task
    // we want to start the first task right away
    osContextSwitch(1,0);
    RESTORE_CONTEXT

    // we will jump to the first task on the list
    asm volatile("ret");
}

void osResumableYield(void) __attribute__ ((naked));
void osResumableYield(void)
{
    SAVE_CONTEXT
    osContextSwitch(1,0);
    RESTORE_CONTEXT

    asm volatile("ret");
}

void osNonResumableYield(void) __attribute__ ((naked));
void osNonResumableYield(void)
{
    SAVE_CONTEXT
    osContextSwitch(0,0);
    RESTORE_CONTEXT

    asm volatile("ret");
}

void osAsmYieldFromTick(void) __attribute__ ((naked));
void osAsmYieldFromTick()
{
    SAVE_CONTEXT
    osContextSwitch(1,1);
    RESTORE_CONTEXT

    asm volatile("ret");
}

void TIMER1_COMPA_vect(void) __attribute__ ((signal, naked));
void TIMER1_COMPA_vect(void)
{
    osAsmYieldFromTick();
    asm volatile("reti");
}
