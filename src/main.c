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

#include <stdio.h>
#include "os.h"

volatile int a = 0;
Mutex *mutex = NULL;

void task01(void* parameters)
{
    while(1) {
        osMutexLock(mutex);

        a = (a + 1) % 32000;
        if(a == 20)
            a++;

        while(a < 2000)
            a++;

        osMutexUnlock(mutex);
    }
}

void task02(void* parameters)
{
    osMutexLock(mutex);

    a = (a + 1) % 32000;
    if(a == 20)
        a++;

    while(a < 2000)
        a++;

    osMutexUnlock(mutex);
}

void task03(void *parameters)
{
    while(1) {
        osMutexLock(mutex);

        a = (a + 1) % 32000;
        if(a == 20)
            a++;

        while(a < 2000)
            a++;

        osMutexUnlock(mutex);
    }
}

int main(int argc, char* argv[])
{
    // initialize operating system
    osInit();

    // create lockable object
    mutex = osMutexCreate();

    // add some tasks
    osCreateTask(task01, NULL, 64, 4);
    osCreateTask(task02, NULL, 64, 4);
    osCreateTask(task03, NULL, 64, 3);

    // run
    osRun();
}
