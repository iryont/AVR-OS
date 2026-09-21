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

#include "mutex.h"
#include "memory.h"

// the first member of the mutex is what tasks waiting for it point to
_Static_assert(offsetof(OsMutex, waiters) == 0, "waiters has to be the first member of OsMutex");

void osMutexInit(OsMutex *mutex)
{
    mutex->waiters = NULL;
    mutex->owner = NULL;
#if OS_CFG_MUTEX_INHERITANCE
    mutex->nextOwned = NULL;
#endif
}

static void osMutexOwn(OsMutex *mutex, OsTask *task)
{
    mutex->owner = task;

#if OS_CFG_MUTEX_INHERITANCE
    mutex->nextOwned = task->mutexes;
    task->mutexes = mutex;
#endif
}

bool osMutexTryLock(OsMutex *mutex, OsTick timeout)
{
    bool locked = true;

    OS_CRITICAL {
        OsTask *task = osCurrentTask;

        if(!mutex->owner) {
            osMutexOwn(mutex, task);
        } else if(timeout == OS_NO_WAIT) {
            locked = false;
        } else {
#if OS_CFG_MUTEX_INHERITANCE
            // the owner must not be held back by tasks less important than we are
            osPriorityRaise(mutex->owner, task->priority);

            // flag is kept by osBlock, that is how others know we wait for a mutex
            task->state |= OS_TASK_MUTEX;
#endif

            // the mutex is handed over to us by the task which unlocks it
            locked = osBlock(&mutex->waiters, timeout);

#if OS_CFG_MUTEX_INHERITANCE
            // we gave up, the owner does not have to hurry because of us anymore
            if(!locked && mutex->owner) {
                OsTask *owner = mutex->owner;
                uint8_t priority = osInheritedPriority(owner);

                if(priority < owner->priority) {
                    owner->priority = priority;
                    osRequeue(owner);
                }
            }
#endif
        }
    }

    return locked;
}

// hand the mutex over to the most important task waiting for it
static void osMutexPass(OsMutex *mutex)
{
    OsTask *waiter = mutex->waiters;
    if(waiter) {
        osMutexOwn(mutex, waiter);
        osWake(waiter, true);
    } else {
        mutex->owner = NULL;
    }
}

#if OS_CFG_MUTEX_INHERITANCE
// mutexes of a task which is gone go to the tasks waiting for them. They would
// wait forever otherwise, and the kernel would keep on raising the priority
// of an owner which does not exist anymore.
void osMutexReleaseAll(OsTask *task)
{
    OsMutex *mutex;
    while((mutex = task->mutexes) != NULL) {
        task->mutexes = mutex->nextOwned;
        osMutexPass(mutex);
    }
}
#endif

void osMutexUnlock(OsMutex *mutex)
{
    OS_CRITICAL {
        OsTask *task = osCurrentTask;

        if(mutex->owner == task) {
#if OS_CFG_MUTEX_INHERITANCE
            // not ours anymore
            OsMutex **owned = &task->mutexes;
            while(*owned && *owned != mutex)
                owned = &(*owned)->nextOwned;

            if(*owned)
                *owned = mutex->nextOwned;
#endif

            osMutexPass(mutex);

#if OS_CFG_MUTEX_INHERITANCE
            // back to our own priority, unless another mutex says otherwise
            task->priority = osInheritedPriority(task);
#endif

            osSchedule(0);
        }
    }
}

#if OS_CFG_DYNAMIC
OsMutex *osMutexCreate(void)
{
    OsMutex *mutex = (OsMutex*)osMalloc(sizeof(OsMutex));
    if(mutex)
        osMutexInit(mutex);

    return mutex;
}

void osMutexDestroy(OsMutex *mutex)
{
    osFree(mutex);
}
#endif
