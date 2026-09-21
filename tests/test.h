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

#ifndef TEST_H
#define TEST_H

#include "../system/system.h"

// tests are firmwares executed by simavr (see run.py). Text goes to the
// console of the simulator, the result of a test is its exit code.

// stack of a task used by tests
#define TEST_STACK 256

void testPrint(const char *text);
void testNumber(const char *name, uint32_t number);
void testPass(void) __attribute__ ((noreturn));
void testFail(uint16_t line) __attribute__ ((noreturn));

#define TEST_ASSERT(condition)      \
    do {                            \
        if(!(condition))            \
            testFail(__LINE__);     \
    } while(0)

// limits on how long things take make sense for optimized code only, a build
// without optimization (-O0) is still expected to work, just not that fast.
#ifdef __OPTIMIZE__
#define TEST_ASSERT_FAST(condition) TEST_ASSERT(condition)
#else
#define TEST_ASSERT_FAST(condition) (void)(condition)
#endif

// keeps the processor busy for the given number of ticks (of its own time)
void testWork(uint16_t ticks);

// called instead of failing the test when a stack overflow is detected
extern void (*testStackOverflow)(OsTask *task);

// second source of interrupts, fires every given number of timer counts
void testInterruptStart(uint16_t counts);
void testInterruptStop(void);

// number of processor cycles, overflows every 65536
void testCyclesStart(void);
#define testCycles() TCNT3

#endif
