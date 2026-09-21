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

// Tasks: suspend and resume, priorities, dynamic tasks, heap, stack overflow.

// VARIANT: noaging -DOS_CFG_AGING=0
// VARIANT: high@atmega2560 tests/filler.c -Wl,--undefined=highFiller

#include <stdlib.h>
#include <string.h>
#include "test.h"

#define TASKS 3

static uint8_t memoryMain[OS_TASK_MEMORY(TEST_STACK)];
static uint8_t memory[TASKS][OS_TASK_MEMORY(TEST_STACK)];
static OsTask *tasks[TASKS];

static OsSemaphore semaphore;
static OsMutex mutex;

static volatile uint16_t counter = 0;
static volatile uint8_t result = 0xff;
static volatile uint8_t finished = 0;
static volatile uint16_t resumed = 0;

static volatile char order[16];
static volatile uint8_t orders = 0;

static void mark(char id)
{
    OS_CRITICAL {
        order[orders++] = id;
        order[orders] = 0;
    }
}

static void counting(void *param)
{
    (void)param;

    while(1) {
        OS_CRITICAL {
            counter++;
        }
    }
}

static void waiting(void *param)
{
    (void)param;

    result = osSemaphoreTake(&semaphore, OS_WAIT_FOREVER);
    finished = 1;
}

static void sleeping(void *param)
{
    (void)param;

    while(1) {
        osTaskSuspend(NULL);
        resumed++;
    }
}

OS_ISR(TIMER1_COMPA_vect)
{
    osTaskResumeFromISR(tasks[0]);
}

static void marker(void *param)
{
    mark((char)(uintptr_t)param);
}

static void locker(void *param)
{
    osMutexLock(&mutex);
    mark((char)(uintptr_t)param);
    osMutexUnlock(&mutex);
}

#if OS_CFG_DYNAMIC
static void dynamic(void *param)
{
    // dynamic tasks can create dynamic tasks
    uint8_t depth = (uint8_t)(uintptr_t)param;
    if(depth)
        TEST_ASSERT(osTaskCreateDynamic(dynamic, (void*)(uintptr_t)(depth - 1), TEST_STACK, 3 + depth) != NULL);

    void *block = osMalloc(40 + depth);
    TEST_ASSERT(block != NULL);
    memset(block, depth, 40 + depth);
    osDelay(1 + depth);
    osFree(block);

    OS_CRITICAL {
        finished++;
    }
}

static void forever(void *param)
{
    (void)param;

    while(1)
        osDelay(1000);
}
#endif

#if OS_CFG_STACK_CHECK
static volatile uint8_t overflows = 0;

static void overflow(OsTask *task)
{
    // everything the test wanted to see
    TEST_ASSERT(task == tasks[1]);
    overflows++;

    testPass();
}

static uint16_t recursive(uint16_t depth)
{
    volatile uint8_t local[16];
    memset((void*)local, (uint8_t)depth, sizeof(local));

    // stack is checked whenever the task gives the processor away
    osDelay(1);

    // way more than the stack can take
    if(depth == 1000)
        return 0;

    return recursive(depth + 1) + local[depth % 16];
}

static void reckless(void *param)
{
    (void)param;
    recursive(1);
}

static uint16_t deep(uint8_t depth)
{
    volatile uint8_t local[32];
    memset((void*)local, depth, sizeof(local));

    return depth ? deep(depth - 1) + local[depth] : local[0];
}

static void careful(void *param)
{
    (void)param;

    deep(3);
    osTaskSuspend(NULL);
}

#ifdef __OPTIMIZE__
static volatile uint16_t frugalSpins = 0;

// does nothing which needs a stack, but it gets preempted and it sleeps
static void frugal(void *param)
{
    (void)param;

    while(1) {
        for(uint16_t i = 0; i < 20000; i++)
            frugalSpins++;

        osDelay(1);
    }
}
#endif
#endif

static void mainTask(void *param)
{
    (void)param;

    // suspended task does not run, no matter what it was doing
    tasks[0] = osTaskCreate(counting, NULL, memory[0], sizeof(memory[0]), 1);
    osDelay(5);

    osTaskSuspend(tasks[0]);
    TEST_ASSERT(osTaskGetState(tasks[0]) == OS_TASK_SUSPENDED);

    uint16_t before = counter;
    TEST_ASSERT(before > 0);
    osDelay(5);
    TEST_ASSERT(counter == before);

    // suspending it once again changes nothing, neither does resuming a task which is not suspended
    osTaskSuspend(tasks[0]);
    osTaskResume(tasks[0]);
    TEST_ASSERT(osTaskGetState(tasks[0]) == OS_TASK_READY);
    osTaskResume(tasks[0]);

    osDelay(5);
    TEST_ASSERT(counter > before);
    osTaskDelete(tasks[0]);

    // task which waits for something stops waiting once suspended, it does not get it
    osSemaphoreInit(&semaphore, 0, 1);
    tasks[0] = osTaskCreate(waiting, NULL, memory[0], sizeof(memory[0]), 20);
    TEST_ASSERT(osTaskGetState(tasks[0]) == OS_TASK_BLOCKED);
    TEST_ASSERT(semaphore.waiters == tasks[0]);

    osTaskSuspend(tasks[0]);
    TEST_ASSERT(semaphore.waiters == NULL);

    osSemaphoreGive(&semaphore);
    TEST_ASSERT(osSemaphoreCount(&semaphore) == 1);
    TEST_ASSERT(!finished);

    osTaskResume(tasks[0]);
    TEST_ASSERT(finished && result == false);
    TEST_ASSERT(osSemaphoreCount(&semaphore) == 1);

    // task which sleeps can be suspended as well, it does not wake up on its own
    tasks[0] = osTaskCreate(marker, (void*)'x', memory[0], sizeof(memory[0]), 1);
    osTaskSuspend(tasks[0]);
    osDelay(3);
    TEST_ASSERT(orders == 0);
    osTaskResume(tasks[0]);
    osDelay(2);
    TEST_ASSERT(strcmp((const char*)order, "x") == 0);
    orders = 0;

    // task which suspends itself is resumed by an interrupt
    tasks[0] = osTaskCreate(sleeping, NULL, memory[0], sizeof(memory[0]), 20);
    testInterruptStart(3001);
    osDelay(200);
    testInterruptStop();

    testNumber("resumed", resumed);
    TEST_ASSERT(resumed > 500);
    osTaskDelete(tasks[0]);

    // priorities: tasks run in the order of their priorities, not in the order of creation
    tasks[0] = osTaskCreate(marker, (void*)'a', memory[0], sizeof(memory[0]), 3);
    tasks[1] = osTaskCreate(marker, (void*)'b', memory[1], sizeof(memory[1]), 5);
    tasks[2] = osTaskCreate(marker, (void*)'c', memory[2], sizeof(memory[2]), 4);
    osDelay(2);
    TEST_ASSERT(strcmp((const char*)order, "bca") == 0);
    orders = 0;

    // unless the priorities are changed
    tasks[0] = osTaskCreate(marker, (void*)'a', memory[0], sizeof(memory[0]), 3);
    tasks[1] = osTaskCreate(marker, (void*)'b', memory[1], sizeof(memory[1]), 5);
    tasks[2] = osTaskCreate(marker, (void*)'c', memory[2], sizeof(memory[2]), 4);
    osTaskSetPriority(tasks[0], 6);
    osTaskSetPriority(tasks[1], 2);
    TEST_ASSERT(osTaskGetPriority(tasks[0]) == 6);
    TEST_ASSERT(osTaskGetPriority(tasks[1]) == 2);
    osDelay(2);
    TEST_ASSERT(strcmp((const char*)order, "acb") == 0);
    orders = 0;

    // task which becomes more important than we are runs right away
    tasks[0] = osTaskCreate(marker, (void*)'a', memory[0], sizeof(memory[0]), 3);
    TEST_ASSERT(orders == 0);
    osTaskSetPriority(tasks[0], 50);
    TEST_ASSERT(strcmp((const char*)order, "a") == 0);
    orders = 0;

    // and so do others once we are less important than they are
    tasks[0] = osTaskCreate(marker, (void*)'a', memory[0], sizeof(memory[0]), 3);
    tasks[1] = osTaskCreate(marker, (void*)'b', memory[1], sizeof(memory[1]), 5);
    osTaskSetPriority(NULL, 4);
    TEST_ASSERT(strcmp((const char*)order, "b") == 0);
    TEST_ASSERT(osTaskGetPriority(NULL) == 4);
    osTaskSetPriority(NULL, 10);
    osDelay(2);
    TEST_ASSERT(strcmp((const char*)order, "ba") == 0);
    orders = 0;

    // priority matters for tasks which wait as well: the last one becomes the first one
    osMutexLock(&mutex);
    tasks[0] = osTaskCreate(locker, (void*)'a', memory[0], sizeof(memory[0]), 5);
    tasks[1] = osTaskCreate(locker, (void*)'b', memory[1], sizeof(memory[1]), 4);
    tasks[2] = osTaskCreate(locker, (void*)'c', memory[2], sizeof(memory[2]), 3);
    osDelay(1);
    TEST_ASSERT(mutex.waiters == tasks[0]);
    osTaskSetPriority(tasks[2], 6);
    TEST_ASSERT(mutex.waiters == tasks[2]);
    osMutexUnlock(&mutex);
    osDelay(2);
    TEST_ASSERT(strcmp((const char*)order, "cab") == 0);
    orders = 0;

#if OS_CFG_DYNAMIC
    // heap is what is left between the variables and the stack of main()
    TEST_ASSERT(__malloc_heap_end == (char*)(RAMEND + 1 - OS_CFG_MAIN_STACK_SIZE));

    // memory of dynamic tasks is released once they are gone
    void *probe = osMalloc(16);
    TEST_ASSERT(probe != NULL);
    osFree(probe);

    finished = 0;
    for(uint8_t i = 0; i < 20; i++) {
        TEST_ASSERT(osTaskCreateDynamic(dynamic, (void*)3, TEST_STACK, 3) != NULL);
        osDelay(i % 4);
    }

    osDelay(20);
    TEST_ASSERT(finished == 20 * 4);

    // no matter how they are gone
    OsTask *victim = osTaskCreateDynamic(forever, NULL, TEST_STACK, 30);
    TEST_ASSERT(victim != NULL);
    TEST_ASSERT(osTaskGetState(victim) == OS_TASK_BLOCKED);
    osTaskDelete(victim);
    osDelay(2);

    // everything is back, so the very same block is available again
    void *again = osMalloc(16);
    TEST_ASSERT(again == probe);
    osFree(again);

    // more than there is
    TEST_ASSERT(osTaskCreateDynamic(forever, NULL, 60000, 3) == NULL);
    TEST_ASSERT(osMalloc(60000) == NULL);

    again = osMalloc(16);
    TEST_ASSERT(again == probe);
    osFree(again);
#endif

#if OS_CFG_STACK_CHECK
    // stack which has never been touched
    tasks[0] = osTaskCreate(careful, NULL, memory[0], sizeof(memory[0]), 1);
    uint16_t unused = osTaskStackFree(tasks[0]);
    TEST_ASSERT(unused == TEST_STACK - OS_CONTEXT_SIZE - 2 * OS_PC_SIZE);

    osDelay(2);
    uint16_t left = osTaskStackFree(tasks[0]);
    testNumber("stack left", left);
    TEST_ASSERT(left < unused - 4 * 32 && left > 0);
    osTaskDelete(tasks[0]);

#ifdef __OPTIMIZE__
    // the smallest stack there is happens to be enough for a task which has
    // no needs. Not without optimization though, when everything is kept on
    // the stack - which is detected as the overflow it is.
    static uint8_t tiny[2 * OS_TASK_MEMORY(OS_STACK_MIN)];
    tasks[0] = osTaskCreate(frugal, NULL, tiny + OS_TASK_MEMORY(OS_STACK_MIN), OS_TASK_MEMORY(OS_STACK_MIN), 1);
    TEST_ASSERT(tasks[0] != NULL);
    TEST_ASSERT(osTaskCreate(frugal, NULL, tiny, OS_TASK_MEMORY(OS_STACK_MIN) - 1, 1) == NULL);

    osTaskSetPriority(NULL, 1);
    osDelay(30);
    osTaskSetPriority(NULL, 10);

    testNumber("stack min", OS_STACK_MIN);
    testNumber("stack min left", osTaskStackFree(tasks[0]));
    TEST_ASSERT(frugalSpins > 1000);
    osTaskDelete(tasks[0]);
#endif

    // our own stack, and the one which belongs to main() and interrupts
    testNumber("stack left (test)", osTaskStackFree(NULL));
    testNumber("stack left (idle)", osTaskStackFree(&osIdleTask));
    TEST_ASSERT(osTaskStackFree(NULL) > 0 && osTaskStackFree(NULL) < TEST_STACK);
    TEST_ASSERT(osTaskStackFree(&osIdleTask) > 0 && osTaskStackFree(&osIdleTask) < OS_CFG_MAIN_STACK_SIZE);

    // stack overflow, the test ends once it has been detected. It is detected
    // after the fact, so whatever is located below the stack is damaged by
    // then: this is what the memory of another task (not in use) is for.
    testStackOverflow = overflow;
    tasks[1] = osTaskCreate(reckless, NULL, memory[1], sizeof(memory[1]), 20);
    osDelay(100);

    TEST_ASSERT(overflows > 0);
#endif

    testPass();
}

int main(void)
{
    osInit();

    osTaskCreate(mainTask, NULL, memoryMain, sizeof(memoryMain), 10);
    osRun();
}
