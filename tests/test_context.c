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

// context switch: a task gets back every single register the way it left it.
//
// the switch saves only a part of the registers and counts on the interrupt
// handler to take care of the rest, so this is what has to be proven: tasks
// below keep a unique value in every register (status register and RAMPZ
// included) and check them all the time, while being preempted at random
// points by a fast tick and by a second interrupt which wakes another task.

// FLAGS: -DOS_CFG_TICK_HZ=5000
// VARIANT: noaging -DOS_CFG_AGING=0
// VARIANT: timer3 -DOS_CFG_TICK_TIMER=3 -DOS_CFG_TICK_HZ=7919
// VARIANT: high@atmega2560 tests/filler.c -Wl,--undefined=highFiller

#include "test.h"

// number of ticks each part of the test takes
#define CONTEXT_TICKS 2500

static uint8_t memoryMain[OS_TASK_MEMORY(TEST_STACK)];
static uint8_t memoryWaker[OS_TASK_MEMORY(TEST_STACK)];
static uint8_t memorySpin[3][OS_TASK_MEMORY(TEST_STACK)];
static uint8_t memoryCall[3][OS_TASK_MEMORY(TEST_STACK)];

volatile uint8_t progressA __attribute__ ((used)) = 0;
volatile uint8_t progressB __attribute__ ((used)) = 0;
volatile uint8_t progressC __attribute__ ((used)) = 0;

static volatile uint16_t calls[3];
static volatile uint16_t wakes = 0;

static OsSemaphore semaphore;

void contextReport(uint8_t id) __attribute__ ((used, noreturn));
void contextReport(uint8_t id)
{
    // number of the register, 101-102: carry, 103: t flag, 104: rampz
    testNumber("lost", id);
    testFail(0);
}

// registers are in no shape to run anything compiled, not before r1 is zero again
void contextFail(void) __attribute__ ((naked, used));
void contextFail(void)
{
    asm volatile (
        "clr   r1                       \n\t"
        "jmp   contextReport            \n\t"
    );
}

#define FAIL(id)                                \
    "breq  1f                           \n\t"   \
    "ldi   r24, " #id "                 \n\t"   \
    "jmp   contextFail                  \n\t"   \
    "1:                                 \n\t"

// r16-r31 hold seed + number of the register, r0-r15 are copies of these
#define LOAD_HIGH(n, seed)  "ldi   r" #n ", " #seed " + " #n "\n\t"
#define LOAD_LOW(n, m)      "mov   r" #n ", r" #m "\n\t"
#define CHECK_HIGH(n, seed) "cpi   r" #n ", " #seed " + " #n "\n\t" FAIL(n)
#define CHECK_LOW(n, m)     "cp    r" #n ", r" #m "\n\t" FAIL(n)

#define CONTEXT_SPIN(name, progress, seed, rampz, flagSet, flagBranch)                  \
    static void name(void *param __attribute__ ((unused))) __attribute__ ((naked));    \
    static void name(void *param __attribute__ ((unused)))                              \
    {                                                                                   \
        asm volatile (                                                                  \
            "ldi   r16, " #rampz "          \n\t"                                       \
            "out   __RAMPZ__, r16           \n\t"                                       \
            LOAD_HIGH(16, seed) LOAD_HIGH(17, seed) LOAD_HIGH(18, seed)                 \
            LOAD_HIGH(19, seed) LOAD_HIGH(20, seed) LOAD_HIGH(21, seed)                 \
            LOAD_HIGH(22, seed) LOAD_HIGH(23, seed) LOAD_HIGH(24, seed)                 \
            LOAD_HIGH(25, seed) LOAD_HIGH(26, seed) LOAD_HIGH(27, seed)                 \
            LOAD_HIGH(28, seed) LOAD_HIGH(29, seed) LOAD_HIGH(30, seed)                 \
            LOAD_HIGH(31, seed)                                                         \
            LOAD_LOW(0, 16) LOAD_LOW(1, 17) LOAD_LOW(2, 18) LOAD_LOW(3, 19)             \
            LOAD_LOW(4, 20) LOAD_LOW(5, 21) LOAD_LOW(6, 22) LOAD_LOW(7, 23)             \
            LOAD_LOW(8, 24) LOAD_LOW(9, 25) LOAD_LOW(10, 26) LOAD_LOW(11, 27)           \
            LOAD_LOW(12, 28) LOAD_LOW(13, 29) LOAD_LOW(14, 30) LOAD_LOW(15, 31)         \
            flagSet "                       \n\t"                                       \
            "0:                             \n\t"                                       \
            "sec                            \n\t"                                       \
            "nop                            \n\t"                                       \
            "nop                            \n\t"                                       \
            "nop                            \n\t"                                       \
            "nop                            \n\t"                                       \
            "brcs  1f                       \n\t"                                       \
            "ldi   r24, 101                 \n\t"                                       \
            "jmp   contextFail              \n\t"                                       \
            "1:                             \n\t"                                       \
            "clc                            \n\t"                                       \
            "nop                            \n\t"                                       \
            "nop                            \n\t"                                       \
            "nop                            \n\t"                                       \
            "nop                            \n\t"                                       \
            "brcc  1f                       \n\t"                                       \
            "ldi   r24, 102                 \n\t"                                       \
            "jmp   contextFail              \n\t"                                       \
            "1:                             \n\t"                                       \
            flagBranch " 1f                 \n\t"                                       \
            "ldi   r24, 103                 \n\t"                                       \
            "jmp   contextFail              \n\t"                                       \
            "1:                             \n\t"                                       \
            CHECK_HIGH(16, seed) CHECK_HIGH(17, seed) CHECK_HIGH(18, seed)              \
            CHECK_HIGH(19, seed) CHECK_HIGH(20, seed) CHECK_HIGH(21, seed)              \
            CHECK_HIGH(22, seed) CHECK_HIGH(23, seed) CHECK_HIGH(24, seed)              \
            CHECK_HIGH(25, seed) CHECK_HIGH(26, seed) CHECK_HIGH(27, seed)              \
            CHECK_HIGH(28, seed) CHECK_HIGH(29, seed) CHECK_HIGH(30, seed)              \
            CHECK_HIGH(31, seed)                                                        \
            CHECK_LOW(0, 16) CHECK_LOW(1, 17) CHECK_LOW(2, 18) CHECK_LOW(3, 19)         \
            CHECK_LOW(4, 20) CHECK_LOW(5, 21) CHECK_LOW(6, 22) CHECK_LOW(7, 23)         \
            CHECK_LOW(8, 24) CHECK_LOW(9, 25) CHECK_LOW(10, 26) CHECK_LOW(11, 27)       \
            CHECK_LOW(12, 28) CHECK_LOW(13, 29) CHECK_LOW(14, 30) CHECK_LOW(15, 31)     \
            "in    r16, __RAMPZ__           \n\t"                                       \
            "cpi   r16, " #rampz "          \n\t"                                       \
            FAIL(104)                                                                   \
            "lds   r16, " #progress "       \n\t"                                       \
            "subi  r16, 0xff                \n\t"                                       \
            "sts   " #progress ", r16       \n\t"                                       \
            LOAD_HIGH(16, seed)                                                         \
            "jmp   0b                       \n\t"                                       \
        );                                                                              \
    }

// the second bit of RAMPZ does not exist on devices with less than 256 kB of flash
#ifdef __AVR_3_BYTE_PC__
CONTEXT_SPIN(spinA, progressA, 0x00, 1, "set", "brts")
CONTEXT_SPIN(spinB, progressB, 0x40, 2, "clt", "brtc")
CONTEXT_SPIN(spinC, progressC, 0x90, 3, "set", "brts")
#else
CONTEXT_SPIN(spinA, progressA, 0x00, 1, "set", "brts")
CONTEXT_SPIN(spinB, progressB, 0x40, 0, "clt", "brtc")
CONTEXT_SPIN(spinC, progressC, 0x90, 1, "set", "brts")
#endif

#ifdef __AVR_HAVE_EIJMP_EICALL__
#define CALL_Z "eicall"
#else
#define CALL_Z "icall"
#endif

#define CALL_LOAD(n)                            \
    "ldi   r19, " #n "                  \n\t"   \
    "add   r19, r18                     \n\t"   \
    "mov   r" #n ", r19                 \n\t"

#define CALL_CHECK(n)                           \
    "ldi   r19, " #n "                  \n\t"   \
    "add   r19, r18                     \n\t"   \
    "cp    r" #n ", r19                 \n\t"   \
    "breq  1f                           \n\t"   \
    "ldi   r24, " #n "                  \n\t"   \
    "jmp   9f                           \n\t"   \
    "1:                                 \n\t"

// fills the registers a function has to preserve with seed + number of the
// register, calls the function and returns the number of the register which
// came back different (zero if none of them).
uint8_t contextCall(uint8_t seed, void (*function)(void)) __attribute__ ((naked, noinline));
uint8_t contextCall(uint8_t seed __attribute__ ((unused)), void (*function)(void) __attribute__ ((unused)))
{
    asm volatile (
        "push  r2                       \n\t"
        "push  r3                       \n\t"
        "push  r4                       \n\t"
        "push  r5                       \n\t"
        "push  r6                       \n\t"
        "push  r7                       \n\t"
        "push  r8                       \n\t"
        "push  r9                       \n\t"
        "push  r10                      \n\t"
        "push  r11                      \n\t"
        "push  r12                      \n\t"
        "push  r13                      \n\t"
        "push  r14                      \n\t"
        "push  r15                      \n\t"
        "push  r16                      \n\t"
        "push  r17                      \n\t"
        "push  r28                      \n\t"
        "push  r29                      \n\t"
        "mov   r18, r24                 \n\t"
        "movw  r30, r22                 \n\t"
        CALL_LOAD(2) CALL_LOAD(3) CALL_LOAD(4) CALL_LOAD(5) CALL_LOAD(6) CALL_LOAD(7)
        CALL_LOAD(8) CALL_LOAD(9) CALL_LOAD(10) CALL_LOAD(11) CALL_LOAD(12) CALL_LOAD(13)
        CALL_LOAD(14) CALL_LOAD(15) CALL_LOAD(16) CALL_LOAD(17) CALL_LOAD(28) CALL_LOAD(29)
        "push  r18                      \n\t"
        CALL_Z "                        \n\t"
        "pop   r18                      \n\t"
        CALL_CHECK(2) CALL_CHECK(3) CALL_CHECK(4) CALL_CHECK(5) CALL_CHECK(6) CALL_CHECK(7)
        CALL_CHECK(8) CALL_CHECK(9) CALL_CHECK(10) CALL_CHECK(11) CALL_CHECK(12) CALL_CHECK(13)
        CALL_CHECK(14) CALL_CHECK(15) CALL_CHECK(16) CALL_CHECK(17) CALL_CHECK(28) CALL_CHECK(29)
        "ldi   r24, 0                   \n\t"
        "9:                             \n\t"
        "pop   r29                      \n\t"
        "pop   r28                      \n\t"
        "pop   r17                      \n\t"
        "pop   r16                      \n\t"
        "pop   r15                      \n\t"
        "pop   r14                      \n\t"
        "pop   r13                      \n\t"
        "pop   r12                      \n\t"
        "pop   r11                      \n\t"
        "pop   r10                      \n\t"
        "pop   r9                       \n\t"
        "pop   r8                       \n\t"
        "pop   r7                       \n\t"
        "pop   r6                       \n\t"
        "pop   r5                       \n\t"
        "pop   r4                       \n\t"
        "pop   r3                       \n\t"
        "pop   r2                       \n\t"
        "ret                            \n\t"
    );
}

static void nap(void)
{
    osDelay(1);
}

static void take(void)
{
    osSemaphoreTake(&semaphore, 3);
}

// tasks which give the processor away themselves: yield, delay, semaphore
static void caller(void *param)
{
    uint8_t id = (uint8_t)(uintptr_t)param;
    void (*function)(void) = id == 0 ? osYield : (id == 1 ? nap : take);

    while(1) {
        uint8_t lost = contextCall(0x20 + 0x30 * id, function);
        if(lost)
            contextReport(lost);

        OS_CRITICAL {
            calls[id]++;
        }
    }
}

// the most important task, preempts whoever is running from the interrupt below
static void waker(void *param)
{
    (void)param;

    while(1) {
        osTaskSuspend(NULL);
        wakes++;
    }
}

static OsTask *taskWaker;

OS_ISR(TIMER1_COMPA_vect)
{
    osTaskResumeFromISR(taskWaker);
    osSemaphoreGiveFromISR(&semaphore);
}

static void checkProgress(void)
{
    uint8_t a = progressA;
    uint8_t b = progressB;
    uint8_t c = progressC;

    osDelay(50);

    TEST_ASSERT(progressA != a);
    TEST_ASSERT(progressB != b);
    TEST_ASSERT(progressC != c);
}

static void mainTask(void *param)
{
    (void)param;

    // preemption by the tick only
    osTaskCreate(spinA, NULL, memorySpin[0], sizeof(memorySpin[0]), 1);
    osTaskCreate(spinB, NULL, memorySpin[1], sizeof(memorySpin[1]), 1);
    osTaskCreate(spinC, NULL, memorySpin[2], sizeof(memorySpin[2]), 1);

    osDelay(CONTEXT_TICKS);
    checkProgress();

    // second interrupt, not related to the tick in any way, which switches tasks on its own
    testInterruptStart(2711);

    osDelay(CONTEXT_TICKS);
    checkProgress();
    TEST_ASSERT(wakes > 1000);

    // tasks which switch on their own join the party
    for(uint8_t i = 0; i < 3; i++)
        osTaskCreate(caller, (void*)(uintptr_t)i, memoryCall[i], sizeof(memoryCall[i]), 1);

    osDelay(CONTEXT_TICKS);
    checkProgress();

    OS_CRITICAL {
        testNumber("yield", calls[0]);
        testNumber("delay", calls[1]);
        testNumber("semaphore", calls[2]);
        testNumber("wakes", wakes);

        TEST_ASSERT(calls[0] > 100 && calls[1] > 100 && calls[2] > 100);
    }

    testPass();
}

int main(void)
{
    osInit();
    osSemaphoreInit(&semaphore, 0, 1);

    taskWaker = osTaskCreate(waker, NULL, memoryWaker, sizeof(memoryWaker), 3);
    osTaskCreate(mainTask, NULL, memoryMain, sizeof(memoryMain), 2);

    osRun();
}
