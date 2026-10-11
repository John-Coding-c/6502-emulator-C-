#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

// Anything the CPU can read from / write to.
class Bus {
public:
    virtual ~Bus() = default;
    virtual uint8_t read(uint16_t addr) = 0;
    virtual void write(uint16_t addr, uint8_t value) = 0;
};

// Flat 64 KB RAM.
class Memory : public Bus {
public:
    std::array<uint8_t, 0x10000> data{};

    uint8_t read(uint16_t addr) override { return data[addr]; }
    void write(uint16_t addr, uint8_t value) override { data[addr] = value; }

    void load(uint16_t addr, const uint8_t* bytes, size_t len) {
        for (size_t i = 0; i < len; ++i) data[(addr + i) & 0xFFFF] = bytes[i];
    }
};

// NMOS 6502 core: all 151 official opcodes, decimal mode, cycle counting.
class CPU {
public:
    enum Flag : uint8_t {
        C = 0x01, Z = 0x02, I = 0x04, D = 0x08,
        B = 0x10, U = 0x20, V = 0x40, N = 0x80,
    };

    uint8_t a = 0, x = 0, y = 0, sp = 0xFD, p = U | I;
    uint16_t pc = 0;
    uint64_t cycles = 0;
    bool halted = false;  // set on an illegal/unsupported opcode

    explicit CPU(Bus& bus) : bus(bus) {}

    void reset();   // load PC from $FFFC
    int step();     // execute one instruction, returns cycles taken
    void irq();     // maskable interrupt ($FFFE)
    void nmi();     // non-maskable interrupt ($FFFA)

    bool flag(Flag f) const { return p & f; }

private:
    Bus& bus;
    int extra = 0;  // page-cross / branch penalty cycles for the current instruction

    uint8_t rd(uint16_t addr) { return bus.read(addr); }
    void wr(uint16_t addr, uint8_t v) { bus.write(addr, v); }
    uint16_t rd16(uint16_t addr) { return rd(addr) | (rd(uint16_t(addr + 1)) << 8); }
    uint8_t fetch() { return rd(pc++); }
    uint16_t fetch16() { uint16_t v = rd16(pc); pc += 2; return v; }

    void push(uint8_t v) { wr(0x100 | sp--, v); }
    uint8_t pop() { return rd(0x100 | ++sp); }

    void set(Flag f, bool on) { p = on ? (p | f) : (p & ~f); }
    void setZN(uint8_t v) { set(Z, v == 0); set(N, v & 0x80); }

    // Addressing modes (return effective address)
    uint16_t imm() { return pc++; }
    uint16_t zp() { return fetch(); }
    uint16_t zpx() { return uint8_t(fetch() + x); }
    uint16_t zpy() { return uint8_t(fetch() + y); }
    uint16_t ab() { return fetch16(); }
    uint16_t abx(bool penalty) { return indexed(fetch16(), x, penalty); }
    uint16_t aby(bool penalty) { return indexed(fetch16(), y, penalty); }
    uint16_t izx();
    uint16_t izy(bool penalty);
    uint16_t indexed(uint16_t base, uint8_t idx, bool penalty);

    // Operations
    void adc(uint8_t v);
    void sbc(uint8_t v);
    void cmp(uint8_t reg, uint8_t v);
    void bit(uint8_t v);
    uint8_t asl(uint8_t v);
    uint8_t lsr(uint8_t v);
    uint8_t rol(uint8_t v);
    uint8_t ror(uint8_t v);
    uint8_t inc(uint8_t v) { setZN(++v); return v; }
    uint8_t dec(uint8_t v) { setZN(--v); return v; }
    void branch(bool cond);
    void interrupt(uint16_t vector, bool brk);

    template <class F>
    void rmw(uint16_t addr, F op) { wr(addr, op(rd(addr))); }
};
