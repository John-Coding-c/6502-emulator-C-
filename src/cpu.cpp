#include "cpu.h"

// ---- Addressing helpers ---------------------------------------------------

uint16_t CPU::indexed(uint16_t base, uint8_t idx, bool penalty) {
    uint16_t addr = base + idx;
    if (penalty && ((base ^ addr) & 0xFF00)) extra++;
    return addr;
}

uint16_t CPU::izx() {
    uint8_t z = fetch() + x;
    return rd(z) | (rd(uint8_t(z + 1)) << 8);
}

uint16_t CPU::izy(bool penalty) {
    uint8_t z = fetch();
    uint16_t base = rd(z) | (rd(uint8_t(z + 1)) << 8);
    return indexed(base, y, penalty);
}

// ---- Operations -----------------------------------------------------------

void CPU::adc(uint8_t v) {
    unsigned c = p & C;
    if (p & D) {
        unsigned lo = (a & 0x0F) + (v & 0x0F) + c;
        if (lo > 0x09) lo += 0x06;
        unsigned r = (a & 0xF0) + (v & 0xF0) + (lo > 0x0F ? 0x10 : 0) + (lo & 0x0F);
        set(Z, ((a + v + c) & 0xFF) == 0);
        set(N, r & 0x80);
        set(V, ~(a ^ v) & (a ^ r) & 0x80);
        if ((r & 0x1F0) > 0x90) r += 0x60;
        set(C, (r & 0xFF0) > 0xF0);
        a = uint8_t(r);
    } else {
        unsigned r = a + v + c;
        set(C, r > 0xFF);
        set(V, ~(a ^ v) & (a ^ r) & 0x80);
        a = uint8_t(r);
        setZN(a);
    }
}

void CPU::sbc(uint8_t v) {
    if (p & D) {
        unsigned borrow = (p & C) ? 0 : 1;
        unsigned bin = a - v - borrow;
        int lo = (a & 0x0F) - (v & 0x0F) - int(borrow);
        int hi = (a >> 4) - (v >> 4);
        if (lo < 0) { lo -= 6; hi--; }
        if (hi < 0) hi -= 6;
        // Flags follow the binary result on NMOS parts.
        set(C, bin < 0x100);
        set(V, (a ^ v) & (a ^ bin) & 0x80);
        setZN(uint8_t(bin));
        a = uint8_t((hi << 4) | (lo & 0x0F));
    } else {
        adc(uint8_t(~v));  // binary SBC == ADC of the one's complement
    }
}

void CPU::cmp(uint8_t reg, uint8_t v) {
    set(C, reg >= v);
    setZN(uint8_t(reg - v));
}

void CPU::bit(uint8_t v) {
    set(Z, (a & v) == 0);
    set(N, v & 0x80);
    set(V, v & 0x40);
}

uint8_t CPU::asl(uint8_t v) { set(C, v & 0x80); v <<= 1; setZN(v); return v; }
uint8_t CPU::lsr(uint8_t v) { set(C, v & 0x01); v >>= 1; setZN(v); return v; }

uint8_t CPU::rol(uint8_t v) {
    uint8_t r = uint8_t((v << 1) | (p & C));
    set(C, v & 0x80); setZN(r); return r;
}

uint8_t CPU::ror(uint8_t v) {
    uint8_t r = uint8_t((v >> 1) | ((p & C) << 7));
    set(C, v & 0x01); setZN(r); return r;
}

void CPU::branch(bool cond) {
    int8_t off = int8_t(fetch());
    if (!cond) return;
    uint16_t target = pc + off;
    extra += ((target ^ pc) & 0xFF00) ? 2 : 1;
    pc = target;
}

// ---- Interrupts -----------------------------------------------------------

void CPU::interrupt(uint16_t vector, bool brk) {
    push(pc >> 8);
    push(pc & 0xFF);
    push(p | U | (brk ? B : 0));
    set(I, true);
    pc = rd16(vector);
}

void CPU::reset() {
    a = x = y = 0;
    sp = 0xFD;
    p = U | I;
    pc = rd16(0xFFFC);
    cycles += 7;
    halted = false;
}

void CPU::irq() {
    if (p & I) return;
    interrupt(0xFFFE, false);
    cycles += 7;
}

void CPU::nmi() {
    interrupt(0xFFFA, false);
    cycles += 7;
}

// ---- Instruction dispatch -------------------------------------------------

#define OP(code, cyc, ...) case code: __VA_ARGS__; c = cyc; break;
#define RMW(addr, fn) rmw(addr, [this](uint8_t v) { return fn(v); })

int CPU::step() {
    if (halted) return 0;
    extra = 0;
    int c = 0;
    uint8_t op = fetch();

    switch (op) {
    // Load / store
    OP(0xA9, 2, a = rd(imm()); setZN(a))
    OP(0xA5, 3, a = rd(zp()); setZN(a))
    OP(0xB5, 4, a = rd(zpx()); setZN(a))
    OP(0xAD, 4, a = rd(ab()); setZN(a))
    OP(0xBD, 4, a = rd(abx(true)); setZN(a))
    OP(0xB9, 4, a = rd(aby(true)); setZN(a))
    OP(0xA1, 6, a = rd(izx()); setZN(a))
    OP(0xB1, 5, a = rd(izy(true)); setZN(a))

    OP(0xA2, 2, x = rd(imm()); setZN(x))
    OP(0xA6, 3, x = rd(zp()); setZN(x))
    OP(0xB6, 4, x = rd(zpy()); setZN(x))
    OP(0xAE, 4, x = rd(ab()); setZN(x))
    OP(0xBE, 4, x = rd(aby(true)); setZN(x))

    OP(0xA0, 2, y = rd(imm()); setZN(y))
    OP(0xA4, 3, y = rd(zp()); setZN(y))
    OP(0xB4, 4, y = rd(zpx()); setZN(y))
    OP(0xAC, 4, y = rd(ab()); setZN(y))
    OP(0xBC, 4, y = rd(abx(true)); setZN(y))

    OP(0x85, 3, wr(zp(), a))
    OP(0x95, 4, wr(zpx(), a))
    OP(0x8D, 4, wr(ab(), a))
    OP(0x9D, 5, wr(abx(false), a))
    OP(0x99, 5, wr(aby(false), a))
    OP(0x81, 6, wr(izx(), a))
    OP(0x91, 6, wr(izy(false), a))

    OP(0x86, 3, wr(zp(), x))
    OP(0x96, 4, wr(zpy(), x))
    OP(0x8E, 4, wr(ab(), x))

    OP(0x84, 3, wr(zp(), y))
    OP(0x94, 4, wr(zpx(), y))
    OP(0x8C, 4, wr(ab(), y))

    // Transfers
    OP(0xAA, 2, x = a; setZN(x))
    OP(0xA8, 2, y = a; setZN(y))
    OP(0x8A, 2, a = x; setZN(a))
    OP(0x98, 2, a = y; setZN(a))
    OP(0xBA, 2, x = sp; setZN(x))
    OP(0x9A, 2, sp = x)

    // Stack
    OP(0x48, 3, push(a))
    OP(0x08, 3, push(p | B | U))
    OP(0x68, 4, a = pop(); setZN(a))
    OP(0x28, 4, p = (pop() & ~B) | U)

    // Arithmetic / logic
    OP(0x69, 2, adc(rd(imm())))
    OP(0x65, 3, adc(rd(zp())))
    OP(0x75, 4, adc(rd(zpx())))
    OP(0x6D, 4, adc(rd(ab())))
    OP(0x7D, 4, adc(rd(abx(true))))
    OP(0x79, 4, adc(rd(aby(true))))
    OP(0x61, 6, adc(rd(izx())))
    OP(0x71, 5, adc(rd(izy(true))))

    OP(0xE9, 2, sbc(rd(imm())))
    OP(0xE5, 3, sbc(rd(zp())))
    OP(0xF5, 4, sbc(rd(zpx())))
    OP(0xED, 4, sbc(rd(ab())))
    OP(0xFD, 4, sbc(rd(abx(true))))
    OP(0xF9, 4, sbc(rd(aby(true))))
    OP(0xE1, 6, sbc(rd(izx())))
    OP(0xF1, 5, sbc(rd(izy(true))))

    OP(0x29, 2, a &= rd(imm()); setZN(a))
    OP(0x25, 3, a &= rd(zp()); setZN(a))
    OP(0x35, 4, a &= rd(zpx()); setZN(a))
    OP(0x2D, 4, a &= rd(ab()); setZN(a))
    OP(0x3D, 4, a &= rd(abx(true)); setZN(a))
    OP(0x39, 4, a &= rd(aby(true)); setZN(a))
    OP(0x21, 6, a &= rd(izx()); setZN(a))
    OP(0x31, 5, a &= rd(izy(true)); setZN(a))

    OP(0x09, 2, a |= rd(imm()); setZN(a))
    OP(0x05, 3, a |= rd(zp()); setZN(a))
    OP(0x15, 4, a |= rd(zpx()); setZN(a))
    OP(0x0D, 4, a |= rd(ab()); setZN(a))
    OP(0x1D, 4, a |= rd(abx(true)); setZN(a))
    OP(0x19, 4, a |= rd(aby(true)); setZN(a))
    OP(0x01, 6, a |= rd(izx()); setZN(a))
    OP(0x11, 5, a |= rd(izy(true)); setZN(a))

    OP(0x49, 2, a ^= rd(imm()); setZN(a))
    OP(0x45, 3, a ^= rd(zp()); setZN(a))
    OP(0x55, 4, a ^= rd(zpx()); setZN(a))
    OP(0x4D, 4, a ^= rd(ab()); setZN(a))
    OP(0x5D, 4, a ^= rd(abx(true)); setZN(a))
    OP(0x59, 4, a ^= rd(aby(true)); setZN(a))
    OP(0x41, 6, a ^= rd(izx()); setZN(a))
    OP(0x51, 5, a ^= rd(izy(true)); setZN(a))

    OP(0xC9, 2, cmp(a, rd(imm())))
    OP(0xC5, 3, cmp(a, rd(zp())))
    OP(0xD5, 4, cmp(a, rd(zpx())))
    OP(0xCD, 4, cmp(a, rd(ab())))
    OP(0xDD, 4, cmp(a, rd(abx(true))))
    OP(0xD9, 4, cmp(a, rd(aby(true))))
    OP(0xC1, 6, cmp(a, rd(izx())))
    OP(0xD1, 5, cmp(a, rd(izy(true))))

    OP(0xE0, 2, cmp(x, rd(imm())))
    OP(0xE4, 3, cmp(x, rd(zp())))
    OP(0xEC, 4, cmp(x, rd(ab())))
    OP(0xC0, 2, cmp(y, rd(imm())))
    OP(0xC4, 3, cmp(y, rd(zp())))
    OP(0xCC, 4, cmp(y, rd(ab())))

    OP(0x24, 3, bit(rd(zp())))
    OP(0x2C, 4, bit(rd(ab())))

    // Increment / decrement
    OP(0xE6, 5, RMW(zp(), inc))
    OP(0xF6, 6, RMW(zpx(), inc))
    OP(0xEE, 6, RMW(ab(), inc))
    OP(0xFE, 7, RMW(abx(false), inc))
    OP(0xC6, 5, RMW(zp(), dec))
    OP(0xD6, 6, RMW(zpx(), dec))
    OP(0xCE, 6, RMW(ab(), dec))
    OP(0xDE, 7, RMW(abx(false), dec))
    OP(0xE8, 2, setZN(++x))
    OP(0xC8, 2, setZN(++y))
    OP(0xCA, 2, setZN(--x))
    OP(0x88, 2, setZN(--y))

    // Shifts / rotates
    OP(0x0A, 2, a = asl(a))
    OP(0x06, 5, RMW(zp(), asl))
    OP(0x16, 6, RMW(zpx(), asl))
    OP(0x0E, 6, RMW(ab(), asl))
    OP(0x1E, 7, RMW(abx(false), asl))
    OP(0x4A, 2, a = lsr(a))
    OP(0x46, 5, RMW(zp(), lsr))
    OP(0x56, 6, RMW(zpx(), lsr))
    OP(0x4E, 6, RMW(ab(), lsr))
    OP(0x5E, 7, RMW(abx(false), lsr))
    OP(0x2A, 2, a = rol(a))
    OP(0x26, 5, RMW(zp(), rol))
    OP(0x36, 6, RMW(zpx(), rol))
    OP(0x2E, 6, RMW(ab(), rol))
    OP(0x3E, 7, RMW(abx(false), rol))
    OP(0x6A, 2, a = ror(a))
    OP(0x66, 5, RMW(zp(), ror))
    OP(0x76, 6, RMW(zpx(), ror))
    OP(0x6E, 6, RMW(ab(), ror))
    OP(0x7E, 7, RMW(abx(false), ror))

    // Jumps / calls
    OP(0x4C, 3, pc = fetch16())
    case 0x6C: {  // JMP (ind) with the NMOS page-wrap bug
        uint16_t ptr = fetch16();
        uint16_t hiAddr = (ptr & 0xFF00) | uint8_t(ptr + 1);
        pc = rd(ptr) | (rd(hiAddr) << 8);
        c = 5;
        break;
    }
    case 0x20: {  // JSR
        uint16_t target = fetch16();
        uint16_t ret = pc - 1;
        push(ret >> 8);
        push(ret & 0xFF);
        pc = target;
        c = 6;
        break;
    }
    case 0x60: {  // RTS
        uint8_t lo = pop();
        uint8_t hi = pop();
        pc = uint16_t((lo | (hi << 8)) + 1);
        c = 6;
        break;
    }
    case 0x40: {  // RTI
        p = (pop() & ~B) | U;
        uint8_t lo = pop();
        uint8_t hi = pop();
        pc = lo | (hi << 8);
        c = 6;
        break;
    }
    OP(0x00, 7, pc++; interrupt(0xFFFE, true))  // BRK (skips padding byte)

    // Branches
    OP(0x10, 2, branch(!(p & N)))
    OP(0x30, 2, branch(p & N))
    OP(0x50, 2, branch(!(p & V)))
    OP(0x70, 2, branch(p & V))
    OP(0x90, 2, branch(!(p & C)))
    OP(0xB0, 2, branch(p & C))
    OP(0xD0, 2, branch(!(p & Z)))
    OP(0xF0, 2, branch(p & Z))

    // Flags
    OP(0x18, 2, set(C, false))
    OP(0x38, 2, set(C, true))
    OP(0x58, 2, set(I, false))
    OP(0x78, 2, set(I, true))
    OP(0xB8, 2, set(V, false))
    OP(0xD8, 2, set(D, false))
    OP(0xF8, 2, set(D, true))

    OP(0xEA, 2, (void)0)  // NOP

    default:
        // Unofficial opcode: stop and leave PC pointing at it.
        pc--;
        halted = true;
        return 0;
    }

    c += extra;
    cycles += c;
    return c;
}
