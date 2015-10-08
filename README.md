### Operating system for AVR microcontrollers
* Fixed-priority pre-emptive scheduling
* Process aging to avoid starvation
* Pre-emptive spin-lock mechanism
* Relatively small and simple to learn from

### Supported devices
* ATMega32

### Supported compilers
* GCC (AtmelStudio)

### How to use it?
Check main.c for examples

### Known issues
* It is unsafe to create processes, mutexes, or calling malloc/free when the system is running (fix: add system-side and pre-emptive save allocation mechanism)
