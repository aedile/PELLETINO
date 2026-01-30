/*
 * z80_cpu.c - Z80 CPU Emulator Wrapper
 *
 * Integrates Marat Fayzullin's Z80 emulator with PELLETINO
 */

#include "z80_cpu.h"
#include "Z80.h"
#include "esp_log.h"

static const char *TAG = "Z80";

// Z80 CPU state
static Z80 cpu;

// External callbacks (implemented by pacman_hw.cpp)
extern uint8_t pacman_mem_read(uint16_t addr);
extern void pacman_mem_write(uint16_t addr, uint8_t value);
extern uint8_t pacman_io_read(uint16_t port);
extern void pacman_io_write(uint16_t port, uint8_t value);

void z80_init(void)
{
    ESP_LOGI(TAG, "Initializing Z80 CPU (Fayzullin emulator)");
    
    // Initialize CPU state
    ResetZ80(&cpu);
    
    // Configure for Pac-Man style operation
    cpu.IPeriod = 0;        // We control timing externally
    cpu.IAutoReset = 1;     // Auto-reset interrupt request
    cpu.TrapBadOps = 0;     // Don't trap bad opcodes
    cpu.Trace = 0;          // No tracing
    
    ESP_LOGI(TAG, "Z80 initialized, PC=%04X", cpu.PC.W);
}

void z80_reset(void)
{
    ResetZ80(&cpu);
    ESP_LOGI(TAG, "Z80 reset, PC=%04X", cpu.PC.W);
}

int z80_execute(int cycles)
{
    // Run Z80 for specified cycles using IPeriod/ICount mechanism
    cpu.IPeriod = cycles;
    cpu.ICount = cycles;
    
    // RunZ80 executes until LoopZ80 returns INT_QUIT or ICount expires
    // We return INT_QUIT from LoopZ80 to exit after our cycle budget
    RunZ80(&cpu);
    
    // Return number of cycles actually executed
    return cycles - cpu.ICount;
}

void z80_interrupt(uint8_t vector)
{
    // Trigger interrupt with the given vector
    IntZ80(&cpu, vector);
}

void z80_nmi(void)
{
    // Trigger NMI
    IntZ80(&cpu, INT_NMI);
}

/*
 * Required callbacks for Fayzullin's Z80 emulator
 * These bridge to the Pac-Man hardware implementation
 */

// Memory read
byte RdZ80(register word Addr)
{
    return pacman_mem_read(Addr);
}

// Memory write
void WrZ80(register word Addr, register byte Value)
{
    pacman_mem_write(Addr, Value);
}

// I/O port read
byte InZ80(register word Port)
{
    return pacman_io_read(Port);
}

// I/O port write
void OutZ80(register word Port, register byte Value)
{
    pacman_io_write(Port, Value);
}

// Patch handler (ED FE opcode) - not used in Pac-Man
void PatchZ80(register Z80 *R)
{
    (void)R;
    // No patches needed for Pac-Man
}

// Periodic callback - called when ICount reaches zero
word LoopZ80(register Z80 *R)
{
    (void)R;
    // Return INT_QUIT to exit RunZ80 - we handle timing externally
    return INT_QUIT;
}
