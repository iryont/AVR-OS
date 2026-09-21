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

#include <stdlib.h>
#include "memory.h"

void osPoolInit(OsPool *pool, void *memory, uint8_t blockSize, uint8_t blocks)
{
    // free blocks make a list, each one starts with a pointer to the next one
    uint8_t *block = (uint8_t*)memory;

    pool->free = NULL;
    while(blocks--) {
        *(void**)block = pool->free;
        pool->free = block;
        block += blockSize;
    }
}

void *osPoolAlloc(OsPool *pool)
{
    void *block;

    OS_CRITICAL {
        block = pool->free;
        if(block)
            pool->free = *(void**)block;
    }

    return block;
}

void osPoolFree(OsPool *pool, void *block)
{
    OS_CRITICAL {
        *(void**)block = pool->free;
        pool->free = block;
    }
}

#if OS_CFG_DYNAMIC
// malloc is not reentrant, nobody can get in the way as long as interrupts are disabled
void *osMalloc(size_t size)
{
    void *memory;

    OS_CRITICAL {
        memory = malloc(size);
    }

    // memory of tasks which are gone might be all we need
    if(!memory) {
        osTaskReap();

        OS_CRITICAL {
            memory = malloc(size);
        }
    }

    return memory;
}

void osFree(void *memory)
{
    OS_CRITICAL {
        free(memory);
    }
}
#endif
