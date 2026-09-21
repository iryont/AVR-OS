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

#ifndef OS_CONFIG_H
#define OS_CONFIG_H

// every option below can be overridden from the command line (-DOS_CFG_...=value)

// tick frequency in Hz, all delays and timeouts are expressed in ticks
#ifndef OS_CFG_TICK_HZ
#define OS_CFG_TICK_HZ 1000
#endif

// hardware timer which generates the tick: 0 or 2 (8-bit), 1, 3, 4 or 5 (16-bit)
// 16-bit timers hit the requested frequency exactly for nearly every F_CPU
#ifndef OS_CFG_TICK_TIMER
#define OS_CFG_TICK_TIMER 0
#endif

// 0: 16-bit tick counter (delays up to 65534 ticks), 1: 32-bit tick counter
#ifndef OS_CFG_TICK_32BIT
#define OS_CFG_TICK_32BIT 0
#endif

// value the tick counter starts with, a value close to the overflow makes
// sure the application deals with it, without waiting for it to happen
#ifndef OS_CFG_TICK_START
#define OS_CFG_TICK_START 0
#endif

// tasks of equal priority take turns, one tick each
#ifndef OS_CFG_ROUND_ROBIN
#define OS_CFG_ROUND_ROBIN 1
#endif

// process aging: a task which is ready but cannot get the processor gains one
// priority level every OS_CFG_AGING_TICKS ticks until it runs, so it never starves
#ifndef OS_CFG_AGING
#define OS_CFG_AGING 1
#endif

#ifndef OS_CFG_AGING_TICKS
#define OS_CFG_AGING_TICKS 10
#endif

// aging never lifts a task above this priority, tasks created with a higher
// priority are therefore never delayed by an aged one
#ifndef OS_CFG_AGING_LIMIT
#define OS_CFG_AGING_LIMIT 254
#endif

// the owner of a mutex inherits the priority of the tasks waiting for it
#ifndef OS_CFG_MUTEX_INHERITANCE
#define OS_CFG_MUTEX_INHERITANCE 1
#endif

// stacks are filled with a pattern, overflows are detected on context switch
#ifndef OS_CFG_STACK_CHECK
#define OS_CFG_STACK_CHECK 1
#endif

// tasks created with malloc and a heap which is safe to use from tasks
#ifndef OS_CFG_DYNAMIC
#define OS_CFG_DYNAMIC 1
#endif

// software timers
#ifndef OS_CFG_TIMERS
#define OS_CFG_TIMERS 1
#endif

// the idle task puts the processor to sleep until the next interrupt
#ifndef OS_CFG_IDLE_SLEEP
#define OS_CFG_IDLE_SLEEP 1
#endif

// bytes at the end of the memory reserved for the stack of main(), which is
// used by the idle task (and by interrupts arriving while it runs) later on
#ifndef OS_CFG_MAIN_STACK_SIZE
#define OS_CFG_MAIN_STACK_SIZE 256
#endif

// these are kept in a single byte
#if OS_CFG_AGING_TICKS < 1 || OS_CFG_AGING_TICKS > 255
#error "OS_CFG_AGING_TICKS has to fit between 1 and 255"
#endif

#if OS_CFG_AGING_LIMIT < 1 || OS_CFG_AGING_LIMIT > 254
#error "OS_CFG_AGING_LIMIT has to fit between 1 and 254"
#endif

#if OS_CFG_TICK_HZ < 1
#error "OS_CFG_TICK_HZ has to be greater than zero"
#endif

#endif
