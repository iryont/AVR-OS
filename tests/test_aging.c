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

// Aging: a task which never gives the processor away does not starve others.

// VARIANT: off -DOS_CFG_AGING=0
// VARIANT: limit -DOS_CFG_AGING_LIMIT=4
// VARIANT: norobin -DOS_CFG_ROUND_ROBIN=0
// VARIANT: norobin-off -DOS_CFG_ROUND_ROBIN=0 -DOS_CFG_AGING=0

#include "test.h"

static uint8_t memoryMain[OS_TASK_MEMORY(TEST_STACK)];
static uint8_t memory[3][OS_TASK_MEMORY(TEST_STACK)];

static volatile uint32_t spins[3];

static void spin(void *param)
{
    uint8_t id = (uint8_t)(uintptr_t)param;

    while(1) {
        OS_CRITICAL {
            spins[id]++;
        }
    }
}

static void mainTask(void *param)
{
    (void)param;

    // high never stops, low is three levels below it, peer is as important as high
    OsTask *high = osTaskCreate(spin, (void*)0, memory[0], sizeof(memory[0]), 5);
    OsTask *low = osTaskCreate(spin, (void*)1, memory[1], sizeof(memory[1]), 2);
    OsTask *peer = osTaskCreate(spin, (void*)2, memory[2], sizeof(memory[2]), 5);

    osDelay(2000);

    uint32_t spinsHigh, spinsLow, spinsPeer;
    OS_CRITICAL {
        spinsHigh = spins[0];
        spinsLow = spins[1];
        spinsPeer = spins[2];
    }

    testNumber("high", spinsHigh);
    testNumber("low", spinsLow);
    testNumber("peer", spinsPeer);

    TEST_ASSERT(spinsHigh > 10000);

#if OS_CFG_AGING && OS_CFG_AGING_LIMIT >= 5
    // low gains a level every OS_CFG_AGING_TICKS ticks. Three levels later it
    // is as important as the others, runs for a tick and starts all over.
    uint32_t share = (spinsHigh + spinsPeer) / spinsLow;
    testNumber("share", share);

    TEST_ASSERT(spinsLow > 0);
    TEST_ASSERT(share > 3 * OS_CFG_AGING_TICKS / 2 && share < 3 * OS_CFG_AGING_TICKS * 2);
#else
    // nothing to hope for: either there is no aging or it ends below the others
    TEST_ASSERT(spinsLow == 0);
#endif

#if OS_CFG_ROUND_ROBIN
    // tasks which are equally important share the processor equally, aging or not
    uint32_t difference = spinsHigh > spinsPeer ? spinsHigh - spinsPeer : spinsPeer - spinsHigh;
    TEST_ASSERT(difference < (spinsHigh + spinsPeer) / 20);
#elif OS_CFG_AGING
    // they do not take turns, but the one which waits gets ahead sooner or later
    TEST_ASSERT(spinsPeer > spinsHigh / 2 && spinsPeer < spinsHigh * 2);
#else
    TEST_ASSERT(spinsPeer == 0);
#endif

    // whatever has been gained is gone once the task gets what it needs
    OS_CRITICAL {
        TEST_ASSERT(osInheritedPriority(low) == 2);
        // (equal to the others is enough to take turns with them, one more is needed otherwise)
        TEST_ASSERT(low->priority >= 2 && low->priority <= 5 + !OS_CFG_ROUND_ROBIN);
        TEST_ASSERT(osTaskGetPriority(low) == 2);
        TEST_ASSERT(osTaskGetPriority(high) == 5 && osTaskGetPriority(peer) == 5);
    }

    testPass();
}

int main(void)
{
    osInit();

    osTaskCreate(mainTask, NULL, memoryMain, sizeof(memoryMain), 10);
    osRun();
}
