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

#include "semaphore.h"
#include "memory.h"

void osSemaphoreInit(OsSemaphore *semaphore, uint8_t count, uint8_t limit)
{
    semaphore->waiters = NULL;
    semaphore->count = count;
    semaphore->limit = limit;
}

bool osSemaphoreTake(OsSemaphore *semaphore, OsTick timeout)
{
    bool taken = true;

    OS_CRITICAL {
        if(semaphore->count)
            semaphore->count--;
        else if(timeout == OS_NO_WAIT)
            taken = false;
        else
            taken = osBlock(&semaphore->waiters, timeout);
    }

    return taken;
}

void osSemaphoreGiveFromISR(OsSemaphore *semaphore)
{
    // goes straight to the most important task waiting for it, if there is one
    if(semaphore->waiters)
        osWake(semaphore->waiters, true);
    else if(semaphore->count < semaphore->limit)
        semaphore->count++;
}

void osSemaphoreGive(OsSemaphore *semaphore)
{
    OS_CRITICAL {
        osSemaphoreGiveFromISR(semaphore);
        osSchedule(0);
    }
}

uint8_t osSemaphoreCount(OsSemaphore *semaphore)
{
    return semaphore->count;
}

#if OS_CFG_DYNAMIC
OsSemaphore *osSemaphoreCreate(uint8_t count, uint8_t limit)
{
    OsSemaphore *semaphore = (OsSemaphore*)osMalloc(sizeof(OsSemaphore));
    if(semaphore)
        osSemaphoreInit(semaphore, count, limit);

    return semaphore;
}

void osSemaphoreDestroy(OsSemaphore *semaphore)
{
    osFree(semaphore);
}
#endif
