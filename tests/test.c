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

#include <avr/sleep.h>
#include <util/delay.h>
#include <util/delay_basic.h>
#include "test.h"

#ifdef TEST_HARDWARE

// the real thing: text goes to the serial port (57600 8N1) and the result is
// shown by the LED as well, slow blinking means pass and fast one means fail.
#define BAUD 57600
#include <util/setbaud.h>

// LED of Arduino Mega 2560 (pin 13), the first pin of port B otherwise
#ifdef __AVR_ATmega2560__
#define LED_BIT PB7
#else
#define LED_BIT PB0
#endif

static void testWrite(char byte)
{
    static uint8_t ready = 0;

    if(!ready) {
        UBRR0H = UBRRH_VALUE;
        UBRR0L = UBRRL_VALUE;

#if USE_2X
        UCSR0A = (1 << U2X0);
#else
        UCSR0A = 0;
#endif

        UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
        UCSR0B = (1 << TXEN0);

        ready = 1;
    }

    while(!(UCSR0A & (1 << UDRE0)));
    UDR0 = byte;
}

static void testEnd(uint8_t passed) __attribute__ ((noreturn));
static void testEnd(uint8_t passed)
{
    DDRB |= (1 << LED_BIT);

    while(1) {
        PORTB ^= (1 << LED_BIT);

        if(passed)
            _delay_ms(500);
        else
            _delay_ms(50);
    }
}

#else

// simavr looks for these in the .mmcu section of the firmware: bytes written
// to the console register are printed, the command register makes it quit
#define SIMAVR_CONSOLE  GPIOR0
#define SIMAVR_COMMAND  GPIOR1

#define SIMAVR_TAG_COMMAND  10
#define SIMAVR_TAG_CONSOLE  11

#define SIMAVR_EXIT_PASS    4
#define SIMAVR_EXIT_FAIL    5

struct SimavrRegister {
    uint8_t tag;
    uint8_t length;
    void *address;
} __attribute__ ((packed));

#define SIMAVR_SECTION __attribute__ ((section(".mmcu"), used))

const uint8_t _mmcu[2] SIMAVR_SECTION = { 0, 0 };
const struct SimavrRegister simavrCommand SIMAVR_SECTION = { SIMAVR_TAG_COMMAND, sizeof(void*), (void*)&SIMAVR_COMMAND };
const struct SimavrRegister simavrConsole SIMAVR_SECTION = { SIMAVR_TAG_CONSOLE, sizeof(void*), (void*)&SIMAVR_CONSOLE };

static void testWrite(char byte)
{
    SIMAVR_CONSOLE = byte;
}

static void testEnd(uint8_t passed) __attribute__ ((noreturn));
static void testEnd(uint8_t passed)
{
    SIMAVR_COMMAND = passed ? SIMAVR_EXIT_PASS : SIMAVR_EXIT_FAIL;

    // not every build of simavr knows the commands above, but all of them quit
    // once the processor sleeps with interrupts disabled. The exit code does
    // not tell the result then, the text does.
    sleep_enable();
    sleep_cpu();

    while(1);
}

#endif

void testPrint(const char *text)
{
    OS_CRITICAL {
        while(*text)
            testWrite(*text++);

        // some builds of simavr print the line once they get \r, other ones wait for \n
        testWrite('\r');
        testWrite('\n');
    }
}

void testNumber(const char *name, uint32_t number)
{
    char digits[11];
    char *digit = digits + sizeof(digits) - 1;

    *digit = 0;
    do {
        *--digit = '0' + number % 10;
        number /= 10;
    } while(number);

    OS_CRITICAL {
        while(*name)
            testWrite(*name++);

        testWrite('=');
        testPrint(digit);
    }
}

void testPass(void)
{
    cli();
    testPrint("PASS");
    testEnd(1);
}

void testFail(uint16_t line)
{
    cli();
    testNumber("FAIL at line", line);
    testEnd(0);
}

void testWork(uint16_t ticks)
{
    // four cycles per iteration
    while(ticks--)
        _delay_loop_2(F_CPU / OS_CFG_TICK_HZ / 4);
}

#if OS_CFG_STACK_CHECK
// stack overflow is a failure unless a test expects it
void (*testStackOverflow)(OsTask *task) = NULL;

void osStackOverflowHook(OsTask *task)
{
    if(testStackOverflow) {
        testStackOverflow(task);
        return;
    }

    testPrint("stack overflow");
    testFail(0);
}
#endif

void testInterruptStart(uint16_t counts)
{
    // timer 1, clear on compare match, no prescaler
    OS_CRITICAL {
        TCCR1A = 0;
        TCCR1B = (1 << WGM12);
        OCR1A = counts - 1;
        TCNT1 = 0;
        TIMSK1 = (1 << OCIE1A);
        TCCR1B |= (1 << CS10);
    }
}

void testInterruptStop(void)
{
    TIMSK1 = 0;
    TCCR1B = 0;
}

void testCyclesStart(void)
{
    // timer 3, free running, no prescaler
    TCCR3A = 0;
    TCCR3B = (1 << CS30);
}
