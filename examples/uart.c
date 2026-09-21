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

// serial port: an interrupt handler which passes received bytes to a task
// and a mutex which keeps lines printed by different tasks in one piece.

#include "../system/system.h"

#define BAUD 57600
#include <util/setbaud.h>

static uint8_t memoryEcho[OS_TASK_MEMORY(128)];
static uint8_t memoryClock[OS_TASK_MEMORY(128)];

static OsQueue received;
static uint8_t receivedMemory[OS_QUEUE_MEMORY(sizeof(char), 32)];

static OsMutex output;

// the handler is as short as it gets, the work is done by the task. Thanks
// to OS_ISR the task runs as soon as the handler returns, not a tick later.
OS_ISR(USART0_RX_vect)
{
    char byte = UDR0;

    // the byte is lost if the queue is full, handlers never wait
    osQueueSendFromISR(&received, &byte);
}

static void uartInit(void)
{
    UBRR0H = UBRRH_VALUE;
    UBRR0L = UBRRL_VALUE;

#if USE_2X
    UCSR0A |= (1 << U2X0);
#endif

    // 8 bits, no parity, 1 stop bit
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
    UCSR0B = (1 << RXEN0) | (1 << TXEN0) | (1 << RXCIE0);
}

static void uartWrite(char byte)
{
    while(!(UCSR0A & (1 << UDRE0)));
    UDR0 = byte;
}

static void print(const char *text)
{
    // one line at a time
    osMutexLock(&output);

    while(*text)
        uartWrite(*text++);

    osMutexUnlock(&output);
}

static void echo(void *parameters)
{
    (void)parameters;

    char line[48] = "echo: ";
    uint8_t length = 6;

    while(1) {
        // sleeps until there is something to read
        char byte;
        osQueueReceive(&received, &byte, OS_WAIT_FOREVER);

        if(byte != '\r' && byte != '\n' && length < sizeof(line) - 3) {
            line[length++] = byte;
            continue;
        }

        line[length++] = '\r';
        line[length++] = '\n';
        line[length] = 0;
        print(line);

        length = 6;
    }
}

static void clock(void *parameters)
{
    (void)parameters;

    OsTick wake = osTickCount();
    uint16_t seconds = 0;

    while(1) {
        osDelayUntil(&wake, OS_MS(1000));
        seconds++;

        char line[] = "uptime: 00000 s\r\n";
        uint16_t value = seconds;
        for(uint8_t i = 12; i >= 8; i--) {
            line[i] = '0' + value % 10;
            value /= 10;
        }

        print(line);
    }
}

int main(void)
{
    osInit();
    uartInit();

    osQueueInit(&received, receivedMemory, sizeof(char), 32);

    // whoever deals with the input is the more important one
    osTaskCreate(echo, NULL, memoryEcho, sizeof(memoryEcho), 2);
    osTaskCreate(clock, NULL, memoryClock, sizeof(memoryClock), 1);

    osRun();
}
