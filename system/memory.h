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

#ifndef OS_MEMORY_H
#define OS_MEMORY_H

#include "scheduler.h"

#ifdef __cplusplus
extern "C" {
#endif

// pool of memory blocks of the same size: constant time, no fragmentation
// and safe to use everywhere, interrupt handlers included.
typedef struct {
    void *free;
} OsPool;

// size of the memory needed by a pool, blocks have to hold a pointer at least
#define OS_POOL_MEMORY(blockSize, blocks) ((uint16_t)(blockSize) * (blocks))

void osPoolInit(OsPool *pool, void *memory, uint8_t blockSize, uint8_t blocks);
void *osPoolAlloc(OsPool *pool);
void osPoolFree(OsPool *pool, void *block);

#if OS_CFG_DYNAMIC
// malloc and free which can be used by tasks at any time
void *osMalloc(size_t size);
void osFree(void *memory);
#endif

#ifdef __cplusplus
}
#endif

#endif
