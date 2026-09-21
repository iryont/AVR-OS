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

#include <stdlib.h>
#include <avr/sleep.h>
#include "system.h"

// there is no separate stack for the idle task: main() never gets
// the processor back from osRun, so it becomes the idle task itself.
OsTask osIdleTask;

// end of the variables, provided by the linker
extern uint8_t __heap_start;

void osInit(void)
{
    // stack of main() takes the end of the memory
    uint8_t *stack = (uint8_t*)(RAMEND + 1 - OS_CFG_MAIN_STACK_SIZE);
    if(stack < &__heap_start)
        stack = &__heap_start;

#if OS_CFG_DYNAMIC
    // by default malloc makes sure the heap stays below the stack pointer, which
    // is pointless (and fails every time) once tasks run on their own stacks.
    if(!__malloc_heap_end)
        __malloc_heap_end = (char*)stack;
#endif

#if OS_CFG_STACK_CHECK
    osIdleTask.stack = stack;

    // part of the stack which is in use right now has to stay as it is
    OS_CRITICAL {
        uint8_t *end = (uint8_t*)SP - 16;
        while(stack < end)
            *stack++ = OS_STACK_FILL;
    }
#endif

    // nobody is more important than main() until the system is running
    osIdleTask.state = OS_TASK_RUNNING;
    osIdleTask.priority = 255;
    osIdleTask.basePriority = 255;

    osCurrentTask = &osIdleTask;
}

void osRun(void)
{
    cli();

    osPortTickStart();

#if OS_CFG_IDLE_SLEEP
    set_sleep_mode(SLEEP_MODE_IDLE);
    sleep_enable();
#endif

    // we are the idle task now: the least important one, always ready to run
    osIdleTask.priority = 0;
    osIdleTask.basePriority = 0;

    // give the processor to the most important task
    // we will not get it back until all of them are waiting for something
    osSchedule(0);
    sei();

    while(1) {
#if OS_CFG_DYNAMIC
        // free the memory used by tasks which are gone, if there were any
        if(osTaskReaper)
            osTaskReaper();
#endif
        osIdleHook();
    }
}

void osIdleHook(void) __attribute__ ((weak));
void osIdleHook(void)
{
#if OS_CFG_IDLE_SLEEP
    // any interrupt wakes us up, the tick included
    sleep_cpu();
#endif
}
