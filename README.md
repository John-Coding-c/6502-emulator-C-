# 6502 Emulator

A small, dependency-free MOS 6502 CPU emulator written in C++17.

## Features

- All 151 official NMOS 6502 opcodes and all 13 addressing modes
- Binary-coded decimal (BCD) mode for `ADC` / `SBC`
- Cycle counting, including extra cycles for page crossings and taken branches
- `IRQ`, `NMI`, `BRK`/`RTI` and reset, using the standard vectors at `$FFFA`–`$FFFF`
- Reproduces the hardware's `JMP ($xxFF)` page-wrap bug
- Pluggable `Bus` interface for memory-mapped I/O (64 KB flat RAM by default)
- Character output port: writing a byte to `$F001` prints it to stdout
- Command-line runner: load a raw binary at any address, with optional instruction trace
- Stops on a jump-to-self loop, an illegal opcode, or a cycle limit, then prints the registers
- Built-in "Hello" demo program and a unit test suite
- Passes [Klaus Dormann's 6502 functional test](https://github.com/Klaus2m5/6502_65C02_functional_tests), decimal mode included (`emu6502 -l 0 -s 400 6502_functional_test.bin` ends at `$3469`)

## Build

```bash
cmake -S . -B build
```

```bash
cmake --build build
```

Or build without CMake:

```bash
g++ -std=c++17 -O2 -o emu6502 src/main.cpp src/cpu.cpp
```

## Usage

```
emu6502 [options] [program.bin]
  -l <hex>   load address        (default 0600)
  -s <hex>   start address       (default: load address)
  -r         start from the reset vector at $FFFC
  -c <n>     stop after n cycles
  -t         trace every instruction
```

Run without arguments to see the built-in demo. Run the tests with `ctest --test-dir build`.

## Layout

- `src/cpu.h`, `src/cpu.cpp`: CPU core
- `src/main.cpp`: command-line runner
- `tests/tests.cpp`: unit tests
