# Operating system for Atmel AVR microcontrollers
#
#   make # builds main.c
#   make APP=examples/blink.c # builds another application
#   make MCU=atmega1284p F_CPU=20000000UL
#   make OPTIONS="-DOS_CFG_TICK_HZ=100 -DOS_CFG_AGING=0"
#   make flash PORT=COM3 # uploads it using the bootloader of Arduino Mega 2560
#   make flash PROGRAMMER=usbasp # uploads it using a programmer
#   make flash HEX=file.hex # uploads something else (tests built for the real thing)
#   make list # disassembly mixed with the source
#   make test # runs the tests in simavr (python is needed)
#   make clean
#
# Works the same on Linux and Windows (cmd.exe included). What is needed: avr-gcc with
# avr-libc, make and avrdude for uploading.

MCU         ?= atmega2560
F_CPU       ?= 16000000UL
APP         ?= main.c
OPTIONS     ?=

# avrdude
PROGRAMMER  ?= wiring
BAUDRATE    ?= 115200

ifeq ($(OS),Windows_NT)
PORT        ?= COM3
PYTHON      ?= python
else
PORT        ?= /dev/ttyACM0
PYTHON      ?= python3
endif

BUILD       := build/$(MCU)
NAME        := $(basename $(notdir $(APP)))
TARGET      := $(BUILD)/$(NAME)
HEX         ?= $(TARGET).hex

SOURCES     := $(APP) $(wildcard system/*.c)
OBJECTS     := $(addprefix $(BUILD)/,$(notdir $(SOURCES:.c=.o)))

VPATH       := $(sort $(dir $(SOURCES)))

CFLAGS      := -mmcu=$(MCU) -DF_CPU=$(F_CPU) $(OPTIONS) -Os -std=gnu11 -g -Wall -Wextra \
               -ffunction-sections -fdata-sections -MMD -MP
LDFLAGS     := -mmcu=$(MCU) -Wl,--gc-sections -Wl,-Map=$(TARGET).map

# A bootloader is reached through a serial port and the flash is not ours to erase.
# A programmer finds its way on its own (add whatever it needs to AVRDUDE_FLAGS).
ifneq ($(filter $(PROGRAMMER),wiring arduino),)
AVRDUDE_FLAGS ?= -P $(PORT) -b $(BAUDRATE) -D
else
AVRDUDE_FLAGS ?=
endif

# make uses cmd.exe on Windows unless it finds sh, which speaks a different language
ifeq ($(OS),Windows_NT)
ifeq ($(shell echo $$0),$$0)
CMD         := 1
endif
endif

ifdef CMD
MKDIR        = if not exist "$(subst /,\,$1)" mkdir "$(subst /,\,$1)"
RMDIR        = if exist "$(subst /,\,$1)" rmdir /s /q "$(subst /,\,$1)"
else
MKDIR        = mkdir -p $1
RMDIR        = rm -rf $1
endif

.PHONY: all size list flash test clean

all: $(TARGET).hex size

$(BUILD):
	$(call MKDIR,$(BUILD))

$(BUILD)/%.o: %.c | $(BUILD)
	avr-gcc $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJECTS)
	avr-gcc $(LDFLAGS) $^ -o $@

$(TARGET).hex: $(TARGET).elf
	avr-objcopy -O ihex -R .eeprom $< $@

$(TARGET).lss: $(TARGET).elf
	avr-objdump -h -S $< > $@

size: $(TARGET).elf
	avr-size $<

list: $(TARGET).lss

flash: $(HEX)
	avrdude -p $(MCU) -c $(PROGRAMMER) $(AVRDUDE_FLAGS) -U flash:w:$(HEX):i

test:
	$(PYTHON) tests/run.py

clean:
	$(call RMDIR,build)

-include $(OBJECTS:.o=.d)
