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

#ifndef OS_TASKS_H
#define OS_TASKS_H

#include "asm.h"

#ifdef __cplusplus
extern "C" {
#endif

#if OS_CFG_TICK_32BIT
typedef uint32_t OsTick;
#else
typedef uint16_t OsTick;
#endif

// priority 0 belongs to the idle task, the higher the number the more important the task
#define OS_PRIORITY_MIN 1
#define OS_PRIORITY_MAX 254

// state of a task, flags describe what a blocked task is waiting for
#define OS_TASK_RUNNING     0x00
#define OS_TASK_READY       0x01
#define OS_TASK_BLOCKED     0x02
#define OS_TASK_SUSPENDED   0x03
#define OS_TASK_DEAD        0x04
#define OS_TASK_STATE       0x0f
#define OS_TASK_TIMED       0x10
#define OS_TASK_MUTEX       0x20

struct OsMutex;

typedef struct OsTask OsTask;

struct OsTask {
    // stack pointer of a task which is not running and what the task gets back
    // once woken up. Both are used by the context switch, they have to stay here.
    uint8_t *sp;
    uint8_t result;

    // ready list or the list of tasks waiting for the same object
    OsTask *next;
    OsTask **waitList;

    // list of tasks with a timeout
    OsTask *timerNext;
    OsTick wake;

    // what the task waits for
    union {
        void *item;
        struct {
            uint8_t flags;
            uint8_t options;
        } event;
    } wait;

    // rest stuff
    uint8_t state;
    uint8_t priority;
    uint8_t basePriority;

#if OS_CFG_MUTEX_INHERITANCE
    struct OsMutex *mutexes;
#endif

#if OS_CFG_STACK_CHECK || OS_CFG_DYNAMIC
    uint8_t *stack;
#endif

#if OS_CFG_DYNAMIC
    uint8_t dynamic;
#endif
};

// memory of a task consists of its stack and the task itself (placed on top of it)
#define OS_TASK_MEMORY(stackSize) ((stackSize) + sizeof(OsTask))

// stack of a preempted task has to hold the interrupted function, registers
// saved by the interrupt handler and by the kernel on its way to the switch
#define OS_STACK_MIN (OS_CONTEXT_SIZE + 3 * OS_PC_SIZE + 32)

// pattern the stacks are filled with
#define OS_STACK_FILL 0xa5

OsTask *osTaskCreate(void (*function)(void*), void *param, void *memory, uint16_t size, uint8_t priority);
void osTaskDelete(OsTask *task);
void osTaskExit(void) __attribute__ ((noreturn));

void osTaskSuspend(OsTask *task);
void osTaskResume(OsTask *task);
void osTaskResumeFromISR(OsTask *task);

void osTaskSetPriority(OsTask *task, uint8_t priority);
uint8_t osTaskGetPriority(OsTask *task);
uint8_t osTaskGetState(OsTask *task);
OsTask *osTaskCurrent(void);

#if OS_CFG_STACK_CHECK
uint16_t osTaskStackFree(OsTask *task);
void osStackOverflowHook(OsTask *task);
#endif

#if OS_CFG_DYNAMIC
OsTask *osTaskCreateDynamic(void (*function)(void*), void *param, uint16_t stackSize, uint8_t priority);

// internals of the kernel
extern void (*osTaskReaper)(void);
void osTaskReap(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
