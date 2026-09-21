### Operating system for AVR microcontrollers
* Fixed-priority pre-emptive scheduling, round robin for tasks of equal priority
* Process aging to avoid starvation
* Mutexes with priority inheritance, semaphores, message queues, event flags
* Software timers, memory pools, a heap which is safe to use from tasks
* Everything a task can wait for comes with a timeout
* Interrupt handlers which wake tasks up right away, not with the next tick
* Static or dynamic allocation, the kernel itself never needs a heap
* Stack overflow detection and stack usage measurement
* The processor sleeps whenever there is nothing to do
* Relatively small and simple to learn from: about 1500 lines, 2-4 kB of flash, 21 bytes per task

### Supported devices
* ATmega2560 (and 640/1280/1281/2561 which share its peripherals)
* ATmega1284P (and 164/324/644, pin compatible with ATmega32 this system was born on)

Everything device-specific is located in `system/asm.c`: the context switch and one timer
used as the tick. Other devices of megaAVR family with the same timers (ATmega328P
for instance) should work as they are, they just do not get tested.

### Supported compilers
* avr-gcc, the newer the better. Built from the console on both Linux and Windows, no IDE is needed.
  Tested with 16.1 (from `-O0` to `-O3`, with link time optimization and from a C++ application), 7.3 and 5.4.

### How to use it?
```c
#include "system/system.h"

// memory of a task is its stack and the task itself
static uint8_t memory[OS_TASK_MEMORY(128)];

static void blink(void *parameters)
{
    DDRB |= (1 << PB7);

    while(1) {
        PORTB ^= (1 << PB7);
        osDelay(OS_MS(250));
    }
}

int main(void)
{
    osInit();
    osTaskCreate(blink, NULL, memory, sizeof(memory), 1);
    osRun();
}
```

Check `main.c` and `examples/` for more: a serial port driven by interrupts, a watchdog
which looks after every task, tasks created on demand. Tests in `tests/` use everything
there is to use.

### Tasks
A task is a function which gets a pointer of your choice. It is gone once the function returns.
The higher the priority (1-254) the more important the task: the most important task which
is ready to run is the one running. Priority 0 belongs to the idle task.

| Function | |
|---|---|
| `osTaskCreate(function, param, memory, size, priority)` | New task using given memory, `NULL` if the memory is too small. Safe to call at any time, tasks included. |
| `osTaskCreateDynamic(function, param, stackSize, priority)` | New task using the heap, `NULL` if there is not enough of it. Memory is released once the task is gone. |
| `osTaskDelete(task)` / `osTaskExit()` | Removes the task (`NULL`: the current one). Mutexes it owns go to the tasks waiting for them. |
| `osTaskSuspend(task)` / `osTaskResume(task)` | Suspended task does not run until resumed. Whatever it was waiting for, it stops waiting (and gets a timeout). |
| `osTaskSetPriority(task, priority)` / `osTaskGetPriority(task)` | |
| `osTaskGetState(task)` | `OS_TASK_RUNNING`, `_READY`, `_BLOCKED`, `_SUSPENDED` or `_DEAD` |
| `osTaskCurrent()` | |
| `osTaskStackFree(task)` | Bytes of the stack which have never been used. `&osIdleTask` stands for the stack of `main()` and interrupts. |
| `osDelay(ticks)` | Sleeps for given number of ticks, `osDelay(0)` is `osYield()`. |
| `osDelayUntil(&previous, period)` | Sleeps until `previous + period`, for tasks which do something at regular intervals no matter how long it takes. |
| `osYield()` | Gives the processor to another task of the same priority. |
| `osTickCount()` | Number of ticks since the system is running. |

`OS_MS(ms)` converts milliseconds to ticks. Timeouts are given in ticks, `OS_NO_WAIT` and
`OS_WAIT_FOREVER` are timeouts as well.

### Mutex
```c
OsMutex mutex; // filled with zeros is ready to use, osMutexInit otherwise
osMutexLock(&mutex); // waits as long as it takes
if(osMutexTryLock(&mutex, OS_MS(10))) // or not
osMutexUnlock(&mutex);
```
The owner of a mutex inherits the priority of the most important task waiting for it, through
the whole chain if the owner waits for another mutex itself. It gets back to where it was once it
unlocks the mutex. The mutex goes straight to the most important task waiting for it.
Mutexes are not recursive and they are not for interrupt handlers.

### Semaphore
```c
OsSemaphore semaphore;
osSemaphoreInit(&semaphore, 0, 1); // initial count and the limit, a limit of 1 makes it binary
if(osSemaphoreTake(&semaphore, timeout))
osSemaphoreGive(&semaphore);
osSemaphoreGiveFromISR(&semaphore);
```

### Queue
```c
OsQueue queue;
uint8_t memory[OS_QUEUE_MEMORY(sizeof(Message), 8)];
osQueueInit(&queue, memory, sizeof(Message), 8);

if(osQueueSend(&queue, &message, timeout)) // waits for a room if the queue is full
if(osQueueReceive(&queue, &message, timeout)) // waits for a message if the queue is empty
osQueueSendFromISR(&queue, &message); // never waits, false if there is no room
osQueueReceiveFromISR(&queue, &message);
```
Messages are copied. A message sent to a task which is already waiting for it goes straight to
that task, without visiting the queue.

### Event
```c
OsEvent event; // eight flags, filled with zeros is ready to use
uint8_t flags = osEventWait(&event, 0x03, OS_EVENT_ALL, timeout); // flags which ended the wait, 0 on timeout
osEventSet(&event, 0x01);
osEventSetFromISR(&event, 0x02);
osEventClear(&event, 0xff);
```
Waits for any of the flags (`OS_EVENT_ANY`) or for all of them (`OS_EVENT_ALL`). Flags the task
has been waiting for are cleared once it gets them, unless `OS_EVENT_KEEP` says otherwise.

### Timer
```c
OsTimer timer;
osTimerStart(&timer, delay, period, function, param); // period of 0: just once
osTimerStop(&timer);
```
The function is called by the tick interrupt: it has to be short, it cannot wait for anything
and it is limited to functions with `FromISR` suffix.

### Memory
```c
OsPool pool;
uint8_t memory[OS_POOL_MEMORY(sizeof(Packet), 16)];
osPoolInit(&pool, memory, sizeof(Packet), 16);

Packet *packet = osPoolAlloc(&pool); // NULL if there is nothing left
osPoolFree(&pool, packet);

void *memory = osMalloc(64); // malloc and free which can be used by tasks
osFree(memory);
```
Pools take constant time, never fragment and work everywhere, interrupt handlers included.
Blocks have to be two bytes at least. Every object which comes with `Init` comes with `Create` and `Destroy`
using the heap as well: `osMutexCreate`, `osSemaphoreCreate`, `osQueueCreate`, `osEventCreate`.

### Interrupts
```c
OS_ISR(USART0_RX_vect)
{
    char byte = UDR0;
    osQueueSendFromISR(&queue, &byte);
}
```
`OS_ISR` is `ISR` which switches to the task it woke up as soon as the handler returns. With plain `ISR`
the task waits for the next tick instead, everything else stays the same. Handlers are limited to functions with
`FromISR` suffix (and to those which never wait, like `osPoolAlloc`) and they must not enable interrupts
before calling them. Handlers which do not use the system at all are free to do whatever they want.

### Configuration
Options are located in `system/config.h`, all of them can be set by the compiler as well (`-DOS_CFG_TICK_HZ=100`).

| Option | Default | |
|---|---|---|
| `OS_CFG_TICK_HZ` | 1000 | Frequency of the tick. |
| `OS_CFG_TICK_TIMER` | 0 | Timer used as the tick: 0, 2 (8-bit) or 1, 3, 4, 5 (16-bit, exact for nearly every clock). |
| `OS_CFG_TICK_32BIT` | 0 | 16-bit tick counter (timeouts up to 65534 ticks) or 32-bit one. |
| `OS_CFG_TICK_START` | 0 | Value the counter starts with. Make it overflow soon to see how the application deals with it. |
| `OS_CFG_ROUND_ROBIN` | 1 | Tasks of equal priority take turns, one tick each. |
| `OS_CFG_AGING` | 1 | Process aging, see below. |
| `OS_CFG_AGING_TICKS` | 10 | Ticks a task has to wait to gain one priority level. |
| `OS_CFG_AGING_LIMIT` | 254 | Aging does not go above this priority. Tasks above it are never delayed by an aged one. |
| `OS_CFG_MUTEX_INHERITANCE` | 1 | Priority inheritance. |
| `OS_CFG_STACK_CHECK` | 1 | Stack overflow detection, `osTaskStackFree`. |
| `OS_CFG_DYNAMIC` | 1 | `osMalloc`, `osTaskCreateDynamic` and the rest of functions which need a heap. |
| `OS_CFG_TIMERS` | 1 | Software timers. |
| `OS_CFG_IDLE_SLEEP` | 1 | The idle task puts the processor to sleep (idle mode). |
| `OS_CFG_MAIN_STACK_SIZE` | 256 | End of the memory reserved for the stack of `main()`, the heap ends where it begins. |

Two functions can be replaced with your own: `osIdleHook()` is called by the idle task over and over again (it
must not wait for anything) and `osStackOverflowHook(task)` is called once a stack overflow is detected.

### Tick
The tick is what the time is measured with, it has nothing to do with how fast the system reacts. A task woken up by
an interrupt, a semaphore or a message runs right away - about 450 cycles after the interrupt (28 microseconds at 16 MHz),
no matter how long the tick is. There are only three things the tick is needed for: delays and timeouts, turns of
tasks of equal priority which never sleep, and aging.

One millisecond (the default) is a good choice for nearly everything. What the other ones cost and give at 16 MHz:

| `OS_CFG_TICK_HZ` | Tick | Processor time taken by the tick | The longest delay (16-bit counter) |
|---|---|---|---|
| 100 | 10 ms | 0.1% | 11 minutes |
| 250 | 4 ms | 0.2% | 4 minutes |
| 1000 | 1 ms | 1.0% | 65 seconds |
| 2000 | 0.5 ms | 2.0% | 32 seconds |
| 10000 | 0.1 ms | 9.8% | 6 seconds |

* Slower is for devices running on a battery: every tick wakes the processor up, needed or not.
* Faster is hardly ever the answer. Something which has to happen every 100 microseconds, or exactly 250 microseconds
  after something else, is a job for a hardware timer with `OS_ISR` waking a task up: exact, and free as long as it is not used.
* `osDelay(n)` ends with the n-th tick from now, which is anywhere between n-1 and n ticks away: `osDelay(1)` might
  take no time at all. Add one if it has to be *at least* that long. Use `osDelayUntil` for anything periodic, it does not drift.
* The tick is exact if the clock divides evenly, which 16 MHz does. 20 MHz does not with an 8-bit timer (1001.6 Hz,
  two minutes a day): the compiler says so and `-DOS_CFG_TICK_TIMER=1` fixes it.

### How does it work?
* **A context switch is a function call.** The compiler already assumes half of the registers are lost
  after any call, so `osPortSwitch` saves only the other half: 18 registers instead of 32. Tasks which get
  preempted have the rest saved by the interrupt handler - like every handler which calls a function, this
  is what the compiler does on its own (`RAMPZ` included). Hence there is one way of switching tasks
  instead of two, tasks which wait need less stack and any interrupt handler is able to switch tasks.
* **Nothing is searched for.** Tasks which are ready make a list ordered by priority, so the next one to run is
  always the first one. So do the tasks waiting for the same thing, and the tasks with a timeout are ordered by
  time: all the tick has to do is to look at the first one. Tasks which wait cost nothing.
* **Nobody waits in line twice.** Whatever wakes a task up does its job first: a mutex which gets unlocked belongs
  to the task it woke up, a message is copied straight to the task waiting for it. Tasks never race for what they
  were woken up for, there is nothing to retry and timeouts are exact.
* **Aging** makes tasks which are ready, but less important than the one running, more important over time
  (one level every `OS_CFG_AGING_TICKS` ticks). Once such a task is as important as the running one it gets
  the processor for a single tick and starts all over. A task which never sleeps slows less important tasks
  down instead of stopping them for good: 3 levels below it means 1 tick out of 31.
* **There is no idle stack.** `main()` never gets the processor back from `osRun`, so it becomes the idle task.

### Cost
ATmega2560 at 16 MHz, `-Os`, processor cycles with the time they take in parentheses (16 cycles make a microsecond).
Minimal means aging, inheritance, stack check, timers and dynamic memory disabled. Measured by `tests/test_timing.c`,
which keeps an eye on them.

| | Default | Minimal |
|---|---|---|
| Switch to another task and back (yield) | 515 (32.2 us) | 455 (28.4 us) |
| Semaphore given to the task it wakes up | 420 (26.3 us) | 390 (24.4 us) |
| Interrupt to the task it wakes up | 454 (28.4 us) | 424 (26.5 us) |
| Mutex lock and unlock | 219 (13.7 us) | 115 (7.2 us) |
| Semaphore give and take | 120 (7.5 us) | 110 (6.9 us) |
| Queue send and receive | 372 (23.3 us) | 352 (22.0 us) |
| Tick with nothing to do | 156 (9.8 us) | 132 (8.3 us) |
| Flash: kernel / everything | 2.2 kB / 4.0 kB | 1.5 kB / 2.5 kB |
| Memory: task / mutex / semaphore / queue / event | 21 / 6 / 4 / 8 / 3 | 16 / 4 / 4 / 8 / 3 |

Parts which are not used do not make it to the flash, the heap included (as long as the linker is allowed to drop
them, which is what the Makefile does). Application which blinks takes 2.1 kB of flash
and 160 bytes of memory, stack of the task included. Interrupts are never disabled for long: with a dozen of busy tasks and
a tick ten times faster than usual, handlers had to wait about 1200 cycles at most (75 microseconds).

The context of a task which waits takes 21 bytes of its stack (20 with a 16-bit program counter), about 50 once the task
got preempted - on top of what the task needs itself. Interrupt handlers use the stack of whoever is running. `OS_STACK_MIN` is the smallest
stack accepted, 128 bytes is a good place to start and `osTaskStackFree` tells how much of it is really needed.

### Building
```
make # main.c for ATmega2560 at 16 MHz
make APP=examples/uart.c # another application
make MCU=atmega1284p F_CPU=20000000UL # another microcontroller
make OPTIONS="-DOS_CFG_TICK_HZ=100" # another configuration
make flash PORT=COM3 # upload using the bootloader of Arduino Mega 2560 (/dev/ttyACM0 on Linux)
make flash PROGRAMMER=usbasp # upload using a programmer
make list # disassembly mixed with the source
```
Works the same on Linux and Windows (`cmd.exe` included). All it takes is avr-gcc with avr-libc, make and avrdude.
There are [portable builds](https://github.com/ZakKemble/avr-gcc-build/releases) of the current avr-gcc for both
systems (unpack, add `bin` to `PATH`): the one for Windows comes with make and avrdude, on Linux these two are
a matter of `apt install make avrdude`. Distributions have avr-gcc of their own as well (`gcc-avr avr-libc`), usually
a much older one. To use the system somewhere else compile `system/*.c` along with your application and define `F_CPU`.

### Tests
```
python tests/run.py
```
Every test is a firmware executed by [simavr](https://github.com/buserror/simavr), for both microcontrollers and
in several configurations (71 builds in total, `main.c` and the examples have to build without warnings on top
of that). They check every register survives a context switch while tasks get
preempted by two unrelated interrupts, exact timing of delays and timeouts, priority inheritance (and that
priority inversion really happens without it), overflow of the tick counter, the whole system located above the
first 128 kB of ATmega2560 (where pointers to functions do not reach on their own) and a long run of everything
at once with a tick ten times faster than usual. `--cflags` makes it possible to test other compiler options and
`--toolchain` another compiler.

The very same tests run on the real thing:
```
python tests/run.py --hardware context
make flash HEX=build/hardware/atmega2560/test_context-hardware.hex PORT=COM3
```
They print to the serial port then (57600 8N1) and blink the LED: slowly once passed, fast if failed.
`test_context` is the one to start with on a new board or a new microcontroller, it is the context switch
which depends on the hardware the most.

### Things to keep in mind
* Only tasks can wait. `main()` before `osRun`, the idle hook, interrupt handlers and timers cannot. Whatever would
  make the idle task wait returns a timeout right away instead (which covers handlers and timers only as long as
  it is the idle task they interrupt, so do not count on it).
* A task which owns a mutex and gets suspended keeps it. With `OS_CFG_MUTEX_INHERITANCE` disabled a task which gets
  deleted keeps it as well.
* Handle of a dynamic task is good as long as the task is, do not keep it if the task is going to leave on its own.
* Flags of an event are not counted: setting a flag which is already set changes nothing.
* A stack overflow is detected once the task gives the processor away, which is after the fact. Whatever is
  located below the stack might be damaged by then.
* Functions of avr-libc which keep their state (`strtok`, `rand`...) are shared by all the tasks.
* The idle task uses idle sleep mode. The tick does not run in deeper ones, so they need `osIdleHook` of your own.

### Coming from the previous version?
* ATmega32 is gone, ATmega2560 took its place. So is the project of Atmel Studio, there is a Makefile now.
* `osCreateTask(function, param, stackSize, priority)` is `osTaskCreateDynamic` now (or `osTaskCreate` with your own
  memory), `TaskControlBlock` is `OsTask` and `Mutex` is `OsMutex`.
* `osWait` is `osDelay`. `osResumableYield` and `osNonResumableYield` are `osYield`.
* Timer 1 is free, the tick uses timer 0.
* The known issue is gone: tasks, mutexes and memory can be created and released at any time.
