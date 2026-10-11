#include "../src/cpu.h"

#include <cstdio>
#include <initializer_list>

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                      \
        }                                                                    \
    } while (0)

// Loads code at $0600, runs it until it jumps to itself, returns the CPU state.
struct Harness {
    Memory mem;
    CPU cpu{mem};

    explicit Harness(std::initializer_list<uint8_t> code) {
        uint16_t addr = 0x0600;
        for (uint8_t b : code) mem.data[addr++] = b;
        mem.data[0xFFFC] = 0x00;
        mem.data[0xFFFD] = 0x06;
        cpu.reset();
    }

    void run(int maxSteps = 10000) {
        for (int i = 0; i < maxSteps && !cpu.halted; ++i) {
            uint16_t before = cpu.pc;
            cpu.step();
            if (cpu.pc == before) return;
        }
    }
};

static void test_load_store() {
    std::puts("load/store");
    // LDA #$42; STA $10; LDX $10; STX $0200; LDY #$00; JMP *
    Harness h({0xA9, 0x42, 0x85, 0x10, 0xA6, 0x10, 0x8E, 0x00, 0x02, 0xA0, 0x00, 0x4C, 0x0B, 0x06});
    h.run();
    CHECK(h.cpu.a == 0x42);
    CHECK(h.cpu.x == 0x42);
    CHECK(h.mem.data[0x0200] == 0x42);
    CHECK(h.cpu.flag(CPU::Z));  // from LDY #0
}

static void test_adc_binary() {
    std::puts("adc binary overflow");
    // CLC; LDA #$50; ADC #$50; JMP *
    Harness h({0x18, 0xA9, 0x50, 0x69, 0x50, 0x4C, 0x05, 0x06});
    h.run();
    CHECK(h.cpu.a == 0xA0);
    CHECK(h.cpu.flag(CPU::V));
    CHECK(h.cpu.flag(CPU::N));
    CHECK(!h.cpu.flag(CPU::C));
}

static void test_sbc_binary() {
    std::puts("sbc binary borrow");
    // SEC; LDA #$50; SBC #$F0; JMP *
    Harness h({0x38, 0xA9, 0x50, 0xE9, 0xF0, 0x4C, 0x05, 0x06});
    h.run();
    CHECK(h.cpu.a == 0x60);
    CHECK(!h.cpu.flag(CPU::C));
    CHECK(!h.cpu.flag(CPU::V));
}

static void test_decimal() {
    std::puts("decimal mode");
    // SED; CLC; LDA #$58; ADC #$46; JMP *   -> 58 + 46 = 104
    Harness add({0xF8, 0x18, 0xA9, 0x58, 0x69, 0x46, 0x4C, 0x06, 0x06});
    add.run();
    CHECK(add.cpu.a == 0x04);
    CHECK(add.cpu.flag(CPU::C));

    // SED; SEC; LDA #$46; SBC #$12; JMP *   -> 46 - 12 = 34
    Harness sub({0xF8, 0x38, 0xA9, 0x46, 0xE9, 0x12, 0x4C, 0x06, 0x06});
    sub.run();
    CHECK(sub.cpu.a == 0x34);
    CHECK(sub.cpu.flag(CPU::C));

    // SED; SEC; LDA #$12; SBC #$21; JMP *   -> 12 - 21 = 91, borrow
    Harness neg({0xF8, 0x38, 0xA9, 0x12, 0xE9, 0x21, 0x4C, 0x06, 0x06});
    neg.run();
    CHECK(neg.cpu.a == 0x91);
    CHECK(!neg.cpu.flag(CPU::C));
}

static void test_jsr_rts() {
    std::puts("jsr/rts");
    // 0600 JSR $0608; 0603 LDX #$01; 0605 JMP $0605; 0608 LDA #$42; 060A RTS
    Harness h({0x20, 0x08, 0x06, 0xA2, 0x01, 0x4C, 0x05, 0x06, 0xA9, 0x42, 0x60});
    h.run();
    CHECK(h.cpu.a == 0x42);
    CHECK(h.cpu.x == 0x01);
    CHECK(h.cpu.sp == 0xFD);
    CHECK(h.cpu.pc == 0x0605);
}

static void test_stack() {
    std::puts("stack");
    // LDA #$AB; PHA; LDA #$00; SEC; PHP; CLC; PLP; PLA; JMP *
    Harness h({0xA9, 0xAB, 0x48, 0xA9, 0x00, 0x38, 0x08, 0x18, 0x28, 0x68, 0x4C, 0x0A, 0x06});
    h.run();
    CHECK(h.cpu.a == 0xAB);
    CHECK(h.cpu.flag(CPU::C));  // restored by PLP
    CHECK(h.cpu.flag(CPU::N));  // from PLA
    CHECK(h.cpu.sp == 0xFD);
}

static void test_indirect_modes() {
    std::puts("indirect addressing");
    // LDX #$04; LDY #$01; LDA ($20,X); STA $30; LDA ($40),Y; JMP *
    Harness h({0xA2, 0x04, 0xA0, 0x01, 0xA1, 0x20, 0x85, 0x30, 0xB1, 0x40, 0x4C, 0x0A, 0x06});
    h.mem.data[0x24] = 0x00; h.mem.data[0x25] = 0x03;  // ($24) -> $0300
    h.mem.data[0x0300] = 0x11;
    h.mem.data[0x40] = 0x00; h.mem.data[0x41] = 0x04;  // ($40)+1 -> $0401
    h.mem.data[0x0401] = 0x22;
    h.run();
    CHECK(h.mem.data[0x30] == 0x11);
    CHECK(h.cpu.a == 0x22);
}

static void test_jmp_indirect_bug() {
    std::puts("jmp indirect page wrap");
    // JMP ($10FF) reads low byte from $10FF and high byte from $1000.
    Harness h({0x6C, 0xFF, 0x10});
    h.mem.data[0x10FF] = 0x00;
    h.mem.data[0x1000] = 0x07;
    h.mem.data[0x1100] = 0x08;
    h.mem.data[0x0700] = 0x4C; h.mem.data[0x0701] = 0x00; h.mem.data[0x0702] = 0x07;
    h.run();
    CHECK(h.cpu.pc == 0x0700);
}

static void test_brk_rti() {
    std::puts("brk/rti");
    // 0600 BRK; 0601 (pad); 0602 LDX #$05; JMP *   handler at $0700: LDA #$99; RTI
    Harness h({0x00, 0xEA, 0xA2, 0x05, 0x4C, 0x04, 0x06});
    h.mem.data[0xFFFE] = 0x00; h.mem.data[0xFFFF] = 0x07;
    h.mem.data[0x0700] = 0xA9; h.mem.data[0x0701] = 0x99; h.mem.data[0x0702] = 0x40;
    h.run();
    CHECK(h.cpu.a == 0x99);
    CHECK(h.cpu.x == 0x05);
    CHECK(h.cpu.sp == 0xFD);
}

static void test_interrupts() {
    std::puts("irq/nmi");
    Harness h({0xEA});
    h.mem.data[0xFFFE] = 0x00; h.mem.data[0xFFFF] = 0x07;
    h.mem.data[0xFFFA] = 0x00; h.mem.data[0xFFFB] = 0x08;
    h.cpu.irq();               // masked: I is set after reset
    CHECK(h.cpu.pc == 0x0600);
    h.cpu.nmi();               // never masked
    CHECK(h.cpu.pc == 0x0800);
    CHECK(h.mem.data[0x01FB] & CPU::U);
    CHECK(!(h.mem.data[0x01FB] & CPU::B));
}

static void test_cycles() {
    std::puts("cycle counting");
    // LDX #$01 (2); LDA $10FF,X (4+1 page cross); BNE +0 (2+1 taken)
    Harness h({0xA2, 0x01, 0xBD, 0xFF, 0x10, 0xD0, 0x00});
    h.mem.data[0x1100] = 0x01;
    uint64_t start = h.cpu.cycles;
    CHECK(h.cpu.step() == 2);
    CHECK(h.cpu.step() == 5);
    CHECK(h.cpu.step() == 3);
    CHECK(h.cpu.cycles - start == 10);
}

static void test_illegal_opcode() {
    std::puts("illegal opcode halts");
    Harness h({0xEA, 0x02});
    h.run();
    CHECK(h.cpu.halted);
    CHECK(h.cpu.pc == 0x0601);
}

int main() {
    test_load_store();
    test_adc_binary();
    test_sbc_binary();
    test_decimal();
    test_jsr_rts();
    test_stack();
    test_indirect_modes();
    test_jmp_indirect_bug();
    test_brk_rti();
    test_interrupts();
    test_cycles();
    test_illegal_opcode();

    if (failures) {
        std::printf("\n%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("\nAll tests passed");
    return 0;
}
