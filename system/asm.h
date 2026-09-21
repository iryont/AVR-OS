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

#ifndef OS_ASM_H
#define OS_ASM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

struct OsTask;

// the kernel keeps interrupts disabled while it works on its lists
// and brings back the previous state afterwards
#define OS_CRITICAL ATOMIC_BLOCK(ATOMIC_RESTORESTATE)

// size of a return address on the stack
#ifdef __AVR_3_BYTE_PC__
#define OS_PC_SIZE 3
#else
#define OS_PC_SIZE 2
#endif

// stack taken by a task which is not running: registers r2-r17, r28, r29
// and the address the context switch returns to
#define OS_CONTEXT_SIZE (18 + OS_PC_SIZE)

// interrupts
void osPortTickStart(void);

// stack
uint8_t *osPortStackInit(uint8_t *top, void (*function)(void*), void *param);

// context switch, interrupts have to be disabled
// returns once the previous task is running again, with the result it has been woken up with
uint8_t osPortSwitch(struct OsTask *next, struct OsTask *prev);

#ifdef __cplusplus
}
#endif

#endif
