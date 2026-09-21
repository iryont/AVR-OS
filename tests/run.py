#!/usr/bin/env python3
"""
Builds every test for every supported microcontroller and runs it in simavr.

    python tests/run.py # all tests
    python tests/run.py mutex queue # tests which names contain "mutex" or "queue"
    python tests/run.py --mcu atmega2560 # single microcontroller only
    python tests/run.py -v # show the output of tests which passed as well
    python tests/run.py --hardware context # build tests for the real thing instead

main.c and the examples are not executed, but they have to build without warnings.

With --hardware nothing is executed at all. Tests are built to be uploaded (see "make flash"),
they print to the serial port (57600 8N1) and blink the LED: slowly once they passed, fast
if they failed. Use --f-cpu if the clock differs from the one assumed below.

avr-gcc and simavr are expected to be found in PATH, unless --toolchain (directory
with avr-gcc) and --simavr (the executable) say otherwise.

A test is a firmware which ends with exit code 0 (pass) or 1 (fail). Comments in its
source control how it is built, each of them takes the rest of its line.

Flags used by every build of the test:

    // FLAGS: -DOS_CFG_TICK_HZ=20000

Additional build of the test with given flags, for a single microcontroller only if it is named:

    // VARIANT: name -DOS_CFG_AGING=0
    // VARIANT: name@atmega2560 -DOS_CFG_AGING=0

Skip the default build and run the variants only:

    // ONLY:

Seconds the simulator is given (120 by default):

    // TIMEOUT: 300
"""

import argparse
import concurrent.futures
import glob
import os
import re
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# microcontroller: clock frequency
TARGETS = {
    "atmega2560": 16000000,
    "atmega1284p": 20000000,
}

CFLAGS = [
    "-Os", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-g",
    "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
]

# keeps the section simavr reads its settings from (see test.c)
SIMAVR_FLAGS = ["-Wl,--undefined=_mmcu,--section-start=.mmcu=0x910000"]


class Job:
    def __init__(self, mcu, test, variant, flags, timeout, simulate=True, hardware=False):
        self.mcu = mcu
        self.test = test
        self.variant = variant
        self.flags = flags
        self.timeout = timeout
        self.simulate = simulate
        self.hardware = hardware
        self.name = "%s[%s]" % (os.path.splitext(os.path.basename(test))[0], variant)
        self.passed = False
        self.output = ""
        self.seconds = 0.0


def parse(test):
    """Returns build variants of the test as (name, flags) and its timeout."""
    flags, variants, only, timeout = [], [], False, 120

    with open(test, encoding="utf-8") as source:
        for line in source:
            match = re.match(r"\s*//\s*(FLAGS|VARIANT|ONLY|TIMEOUT):\s*(.*)", line)
            if not match:
                continue

            key, value = match.group(1), match.group(2).split()
            if key == "FLAGS":
                flags += value
            elif key == "VARIANT":
                variants.append((value[0], value[1:]))
            elif key == "ONLY":
                only = True
            elif key == "TIMEOUT":
                timeout = int(value[0])

    if not only:
        variants.insert(0, ("default", []))

    def merge(extra):
        # sources which come along with a variant are given relative to the repository
        extra = [os.path.join(ROOT, flag) if flag.endswith(".c") else flag for flag in extra]

        # a variant is free to redefine what has been defined for all of them
        redefined = {flag.split("=")[0] for flag in extra if flag.startswith("-D")}
        return [flag for flag in flags if flag.split("=")[0] not in redefined] + extra

    return [(name, merge(extra)) for name, extra in variants], timeout


def run(job, gcc, simavr, build, cflags, clock):
    started = time.time()

    directory = os.path.join(build, job.mcu)
    os.makedirs(directory, exist_ok=True)
    elf = os.path.join(directory, job.name.replace("[", "-").replace("]", "") + ".elf")

    sources = sorted(glob.glob(os.path.join(ROOT, "system", "*.c")))
    if job.simulate or job.hardware:
        sources.append(os.path.join(ROOT, "tests", "test.c"))

    sources.append(job.test)

    command = [gcc, "-mmcu=" + job.mcu, "-DF_CPU=%dUL" % (clock or TARGETS[job.mcu])]
    command += CFLAGS + cflags + (SIMAVR_FLAGS if job.simulate else []) + job.flags + sources + ["-o", elf]

    compiled = subprocess.run(command, capture_output=True, text=True)
    if compiled.returncode != 0:
        job.output = "build failed:\n" + compiled.stdout + compiled.stderr
        return job

    if job.hardware:
        # something avrdude can deal with, objcopy is located right next to the compiler
        objcopy = os.path.join(os.path.dirname(gcc), os.path.basename(gcc).replace("gcc", "objcopy"))
        firmware = os.path.splitext(elf)[0] + ".hex"

        converted = subprocess.run([objcopy, "-O", "ihex", "-R", ".eeprom", elf, firmware], capture_output=True, text=True)
        if converted.returncode != 0:
            job.output = "objcopy failed:\n" + converted.stdout + converted.stderr
            return job

        job.output = "make flash MCU=%s HEX=%s" % (job.mcu, os.path.relpath(firmware, ROOT).replace(os.sep, "/"))

    if not job.simulate:
        job.passed = True
        job.seconds = time.time() - started
        return job

    try:
        command = [simavr, "-m", job.mcu, "-f", str(TARGETS[job.mcu]), elf]
        executed = subprocess.run(command, capture_output=True, text=True, timeout=job.timeout)

        job.output = executed.stdout + executed.stderr
        job.passed = executed.returncode == 0 and "PASS" in job.output
    except subprocess.TimeoutExpired as expired:
        job.output = "simulator has been killed after %d seconds\n" % job.timeout
        for stream in (expired.stdout, expired.stderr):
            if stream:
                job.output += stream if isinstance(stream, str) else stream.decode(errors="replace")

    job.seconds = time.time() - started
    return job


def main():
    parser = argparse.ArgumentParser(description="Runs tests of the system in simavr.")
    parser.add_argument("filters", nargs="*", help="run tests which names contain any of these only")
    parser.add_argument("--mcu", action="append", choices=sorted(TARGETS), help="microcontroller to test (default: all)")
    parser.add_argument("--toolchain", help="directory avr-gcc is located in")
    parser.add_argument("--simavr", help="simavr executable")
    parser.add_argument("--build", default=os.path.join(ROOT, "build", "tests"), help="directory for firmwares")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1, help="number of tests executed at once")
    parser.add_argument("--cflags", default="", help="additional compiler flags, e.g. --cflags=\"-O1 -fpack-struct\"")
    parser.add_argument("--hardware", action="store_true", help="build tests for the real thing, do not execute anything")
    parser.add_argument("--f-cpu", type=int, help="clock frequency in Hz, if it differs from the one assumed")
    parser.add_argument("-v", "--verbose", action="store_true", help="show the output of tests which passed")
    arguments = parser.parse_args()

    if arguments.hardware and arguments.build == parser.get_default("build"):
        arguments.build = os.path.join(ROOT, "build", "hardware")

    gcc = os.path.join(arguments.toolchain, "avr-gcc") if arguments.toolchain else "avr-gcc"
    gcc = shutil.which(gcc)
    simavr = shutil.which(arguments.simavr or "simavr")

    if not gcc or not (simavr or arguments.hardware):
        sys.exit("avr-gcc or simavr not found, add them to PATH or use --toolchain and --simavr")

    jobs = []
    for test in sorted(glob.glob(os.path.join(ROOT, "tests", "test_*.c"))):
        if arguments.filters and not any(name in os.path.basename(test) for name in arguments.filters):
            continue

        variants, timeout = parse(test)
        for mcu in arguments.mcu or sorted(TARGETS):
            for variant, flags in variants:
                variant, _, only = variant.partition("@")
                if only and only != mcu:
                    continue

                if not arguments.hardware:
                    jobs.append(Job(mcu, test, variant, flags, timeout))
                elif variant == "default":
                    jobs.append(Job(mcu, test, "hardware", flags + ["-DTEST_HARDWARE"], 0, simulate=False, hardware=True))

    # applications which come with the system
    for application in [os.path.join(ROOT, "main.c")] + sorted(glob.glob(os.path.join(ROOT, "examples", "*.c"))):
        if arguments.hardware:
            break

        if arguments.filters and not any(name in os.path.basename(application) for name in arguments.filters):
            continue

        for mcu in arguments.mcu or sorted(TARGETS):
            jobs.append(Job(mcu, application, "build", [], 0, simulate=False))

    if not jobs:
        sys.exit("no tests found")

    failed = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=arguments.jobs) as pool:
        for job in pool.map(lambda job: run(job, gcc, simavr, arguments.build, arguments.cflags.split(), arguments.f_cpu), jobs):
            status = "PASS" if job.passed else "FAIL"
            if job.hardware and job.passed:
                status = "BUILT"

            print("%-12s %-40s %s  %5.1fs" % (job.mcu, job.name, status, job.seconds))

            if not job.passed or arguments.verbose or job.hardware:
                for line in job.output.splitlines():
                    if line.strip() and not line.startswith("Loaded "):
                        print("    " + line.rstrip())

            if not job.passed:
                failed += 1

    print()
    print("%d of %d passed" % (len(jobs) - failed, len(jobs)))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
