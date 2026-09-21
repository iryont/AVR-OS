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

// not a test, a part of the ones which want the system above the first 128 kB
// of the flash (-Wl,--undefined=highFiller keeps it from being dropped).
//
// pointers to functions are 16 bits wide, which is not enough for the whole
// flash of ATmega2560. The linker makes up for it with a stub located within
// their reach for every function whose address is taken, and that is what the
// system puts on the stack of a new task. Functions have sections of their
// own which come behind plain .text, so 150 kB of nothing placed there moves
// all of them out of reach: the kernel, the tasks and the interrupt handlers.

__asm__ (
    ".section .text,\"ax\",@progbits    \n"
    ".global highFiller                 \n"
    "highFiller:                        \n"
    ".fill 75000, 2, 0x0000             \n"
);
