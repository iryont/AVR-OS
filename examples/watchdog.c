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

// watchdog which looks after every task, not just the system as a whole:
// it is fed only if all the tasks have reported in time. Shows events,
// software timers and tasks created on demand.

#include <avr/wdt.h>
#include "../system/system.h"

// LED of Arduino Mega 2560 (pin 13)
#define LED_DDR     DDRB
#define LED_PORT    PORTB
#define LED_BIT     PB7

// a flag for each task which has to be alive
#define ALIVE_SENSOR    0x01
#define ALIVE_CONTROL   0x02
#define ALIVE_ALL       (ALIVE_SENSOR | ALIVE_CONTROL)

static uint8_t memorySupervisor[OS_TASK_MEMORY(128)];
static uint8_t memorySensor[OS_TASK_MEMORY(128)];
static uint8_t memoryControl[OS_TASK_MEMORY(128)];

// both are ready to use as they are
static OsEvent alive;
static OsTimer heartbeat;

static void supervisor(void *parameters)
{
    (void)parameters;

    wdt_enable(WDTO_2S);

    while(1) {
        // everybody has a second to report. Flags are cleared once we get
        // them all, so the tasks have to report over and over again.
        if(osEventWait(&alive, ALIVE_ALL, OS_EVENT_ALL, OS_MS(1000)))
            wdt_reset();

        // otherwise somebody is stuck: the watchdog is left alone and
        // it is going to reset the microcontroller in a moment
    }
}

static void sensor(void *parameters)
{
    (void)parameters;

    while(1) {
        // read the sensor here
        osEventSet(&alive, ALIVE_SENSOR);
        osDelay(OS_MS(100));
    }
}

static void job(void *parameters)
{
    (void)parameters;

    // something which takes a while and is not needed very often, there
    // is no point in keeping a stack for it all the time
    osDelay(OS_MS(30));

    // memory of the task is released once it returns
}

static void control(void *parameters)
{
    (void)parameters;

    OsTick wake = osTickCount();
    uint8_t cycle = 0;

    while(1) {
        osDelayUntil(&wake, OS_MS(50));

        // once a second, only if there is enough memory left
        if(++cycle == 20) {
            cycle = 0;
            osTaskCreateDynamic(job, NULL, 96, 1);
        }

        osEventSet(&alive, ALIVE_CONTROL);
    }
}

// called by the tick interrupt: short, never blocks
static void beat(void *parameters)
{
    (void)parameters;
    LED_PORT ^= (1 << LED_BIT);
}

int main(void)
{
    // the watchdog stays enabled after it has reset the microcontroller
    MCUSR = 0;
    wdt_disable();

    osInit();

    LED_DDR |= (1 << LED_BIT);

    // first time after 100 ms, every 500 ms later on
    osTimerStart(&heartbeat, OS_MS(100), OS_MS(500), beat, NULL);

    osTaskCreate(supervisor, NULL, memorySupervisor, sizeof(memorySupervisor), 3);
    osTaskCreate(control, NULL, memoryControl, sizeof(memoryControl), 2);
    osTaskCreate(sensor, NULL, memorySensor, sizeof(memorySensor), 1);

    osRun();
}
