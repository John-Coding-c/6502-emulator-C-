#include "cpu.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

// When double-clicked from Explorer the console closes on exit; wait for a key.
static void pauseIfOwnConsole() {
#ifdef _WIN32
    DWORD pids[2];
    if (GetConsoleProcessList(pids, 2) == 1) {
        std::fprintf(stderr, "\nPress Enter to exit...");
        std::getchar();
    }
#endif
}

// Memory with a character-output port: writing to $F001 prints a byte.
class System : public Memory {
public:
    static constexpr uint16_t PUTCHAR = 0xF001;

    void write(uint16_t addr, uint8_t value) override {
        if (addr == PUTCHAR) {
            std::putchar(value);
            std::fflush(stdout);
            return;
        }
        Memory::write(addr, value);
    }
};

// Built-in demo (assembled at $0600): prints a message, then sums 1..10 into A.
//
//   0600  LDX #$00
//   0602  loop:  LDA msg,X
//   0605         BEQ sum
//   0607         STA $F001
//   060A         INX
//   060B         BNE loop
//   060D  sum:   LDA #$00
//   060F         CLC
//   0610         LDX #$0A
//   0612  add:   STX $10
//   0614         ADC $10
//   0616         DEX
//   0617         BNE add
//   0619         STA $11
//   061B  done:  JMP done
//   061E  msg:   "Hello from the 6502!\n", 0
static const uint8_t DEMO[] = {
    0xA2, 0x00, 0xBD, 0x1E, 0x06, 0xF0, 0x06, 0x8D, 0x01, 0xF0, 0xE8, 0xD0, 0xF5,
    0xA9, 0x00, 0x18, 0xA2, 0x0A, 0x86, 0x10, 0x65, 0x10, 0xCA, 0xD0, 0xF9,
    0x85, 0x11, 0x4C, 0x1B, 0x06,
    'H', 'e', 'l', 'l', 'o', ' ', 'f', 'r', 'o', 'm', ' ', 't', 'h', 'e', ' ',
    '6', '5', '0', '2', '!', '\n', 0x00,
};

static void usage(const char* prog) {
    std::fprintf(stderr,
        "Usage: %s [options] [program.bin]\n"
        "  -l <hex>   load address        (default 0600)\n"
        "  -s <hex>   start address       (default: load address)\n"
        "  -r         start from the reset vector at $FFFC instead\n"
        "  -c <n>     stop after n cycles (default 100000000)\n"
        "  -t         trace every instruction\n"
        "With no program, a built-in demo is run.\n"
        "Execution stops on a jump-to-self loop, an illegal opcode, or the cycle limit.\n",
        prog);
}

static void dumpRegs(const CPU& cpu) {
    char flags[9] = "NV-BDIZC";
    for (int i = 0; i < 8; ++i)
        if (!(cpu.p & (0x80 >> i))) flags[i] = '.';
    std::fprintf(stderr, "PC=%04X A=%02X X=%02X Y=%02X SP=%02X P=%02X [%s] CYC=%llu\n",
                 cpu.pc, cpu.a, cpu.x, cpu.y, cpu.sp, cpu.p, flags,
                 (unsigned long long)cpu.cycles);
}

int main(int argc, char** argv) {
    uint16_t loadAddr = 0x0600;
    long startAddr = -1;
    bool fromReset = false, trace = false;
    unsigned long long maxCycles = 100000000ULL;
    const char* path = nullptr;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        bool hasVal = i + 1 < argc;
        if (!std::strcmp(arg, "-l") && hasVal)      loadAddr = uint16_t(std::strtoul(argv[++i], nullptr, 16));
        else if (!std::strcmp(arg, "-s") && hasVal) startAddr = long(std::strtoul(argv[++i], nullptr, 16) & 0xFFFF);
        else if (!std::strcmp(arg, "-c") && hasVal) maxCycles = std::strtoull(argv[++i], nullptr, 10);
        else if (!std::strcmp(arg, "-r"))           fromReset = true;
        else if (!std::strcmp(arg, "-t"))           trace = true;
        else if (arg[0] == '-') { usage(argv[0]); return 1; }
        else path = arg;
    }

    System mem;
    if (path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) { std::fprintf(stderr, "Cannot open %s\n", path); return 1; }
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
        if (bytes.size() > 0x10000) { std::fprintf(stderr, "Program larger than 64 KB\n"); return 1; }
        mem.load(loadAddr, bytes.data(), bytes.size());
    } else {
        mem.load(loadAddr, DEMO, sizeof(DEMO));
    }

    CPU cpu(mem);
    cpu.reset();  // sets SP/flags and reads $FFFC
    if (!fromReset) cpu.pc = startAddr >= 0 ? uint16_t(startAddr) : loadAddr;

    while (cpu.cycles < maxCycles) {
        uint16_t before = cpu.pc;
        if (trace) {
            std::fprintf(stderr, "%04X  %02X  ", cpu.pc, mem.data[cpu.pc]);
            dumpRegs(cpu);
        }
        cpu.step();
        if (cpu.halted) {
            std::fprintf(stderr, "\nIllegal opcode $%02X at $%04X\n", mem.data[cpu.pc], cpu.pc);
            break;
        }
        if (cpu.pc == before) {
            std::fprintf(stderr, "\nTrapped at $%04X (jump to self)\n", cpu.pc);
            break;
        }
    }
    if (cpu.cycles >= maxCycles) std::fprintf(stderr, "\nCycle limit reached\n");

    dumpRegs(cpu);
    pauseIfOwnConsole();
    return cpu.halted ? 2 : 0;
}
