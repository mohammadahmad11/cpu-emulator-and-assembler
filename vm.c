#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
/* windows only */
#include <Windows.h>
#include <conio.h>  // _kbhit

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    #define IS_HOST_LITTLE_ENDIAN 0
#elif defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    #define IS_HOST_LITTLE_ENDIAN 1
#elif defined(_WIN32)
    #define IS_HOST_LITTLE_ENDIAN 1
#else
    #error "Cannot determine host endianness"
#endif

// Portable 32-bit byte swap
static inline uint32_t swap32(uint32_t val) {
    return ((val & 0x000000FFu) << 24) |
           ((val & 0x0000FF00u) << 8)  |
           ((val & 0x00FF0000u) >> 8)  |
           ((val & 0xFF000000u) >> 24);
}

// Convert host integer to big endian (and vice versa)
static inline uint32_t host_to_big_endian(uint32_t val) {
#if IS_HOST_LITTLE_ENDIAN
    return swap32(val);
#else
    return val;
#endif
}

static inline uint32_t big_endian_to_host(uint32_t val) {
    return host_to_big_endian(val);
}

enum {
    R_R0 = 0,
    R_R1,
    R_R2,
    R_R3,
    R_R4,
    R_R5,
    R_R6,
    R_R7,
    R_PC, /* program counter */
    R_COND,
    R_COUNT
};
enum {
    FL_POS = 1 << 0, /* P */
    FL_ZRO = 1 << 1, /* Z */
    FL_NEG = 1 << 2, /* N */
};
enum {
    OP_NOP = 0,
    OP_BR, /* branch */
    OP_ADD,    /* add  */
    OP_SUB,     /* load */
    OP_ST,     /* store */
    OP_LD,
    OP_OR,    /* jump register */
    OP_AND,    /* bitwise and */
    OP_LDR,    /* load register */
    OP_STR,    /* store register */
    OP_SHL,    /* unused */
    OP_NOT,    /* bitwise not */
    OP_JMP,    /* jump */
    OP_SHR,    /* reserved (unused) */
    OP_TRAP    /* execute trap */
};

enum {
    MR_KBSR = 0x0000FE00, /* keyboard status */
    MR_KBDR = 0x0000FE02  /* keyboard data */
};
enum {
    TRAP_GETC = 0x20,  /* get character from keyboard, not echoed onto the terminal */
    TRAP_OUT = 0x21,   /* output a character */
    TRAP_PUTS = 0x22,  /* output a word string */
    TRAP_IN = 0x23,    /* get character from keyboard, echoed onto the terminal */
    TRAP_PUTSP = 0x24, /* output a byte string */
    TRAP_HALT = 0x25   /* halt the program */
};

#define MEMORY_MAX 65536 // 2^16
uint32_t memory[MEMORY_MAX];  /* 65536 locations */
uint32_t reg[R_COUNT];

HANDLE hStdin = INVALID_HANDLE_VALUE;
DWORD fdwMode, fdwOldMode;

void disable_input_buffering() {
    hStdin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hStdin, &fdwOldMode); /* save old mode */
    fdwMode = fdwOldMode
            ^ ENABLE_ECHO_INPUT  /* no input echo */
            ^ ENABLE_LINE_INPUT; /* return when one or
                                    more characters are available */
    SetConsoleMode(hStdin, fdwMode); /* set new mode */
    FlushConsoleInputBuffer(hStdin); /* clear buffer */
}

void restore_input_buffering() {
    SetConsoleMode(hStdin, fdwOldMode);
}

uint32_t check_key() {
    return WaitForSingleObject(hStdin, 1000) == WAIT_OBJECT_0 && _kbhit();
}
void handle_interrupt(int signal) {
    restore_input_buffering();
    printf("\n");
    exit(-2);
}
uint32_t sign_extend(uint32_t x, int bit_count) {
    if ((x >> (bit_count - 1)) & 1) {
        x |= (0xFFFFFFFF << bit_count);
    }
    return x;
}
void update_flags(uint32_t r) {
    if (reg[r] == 0) {
        reg[R_COND] = FL_ZRO;
    }
    else if (reg[r] >> 31) { /* a 1 in the left-most bit indicates negative */
        reg[R_COND] = FL_NEG;
    } else {
        reg[R_COND] = FL_POS;
    }
}
bool read_big_endian_binary(const char* filename, uint32_t out_array[65536]) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        return false;
    }

    // Check file size to prevent underflow/out-of-bounds reads
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    size_t expected_size_bytes = 65535 * sizeof(uint32_t);
    if ((size_t)file_size < expected_size_bytes) {
        fprintf(stderr, "File size (%ld bytes) is smaller than expected (%size_t bytes)\n", 
                file_size, expected_size_bytes);
        fclose(file);
        return false;
    }

    // Single bulk read into array
    size_t read_count = fread(out_array, sizeof(uint32_t), 65535, file);
    fclose(file);

    if (read_count != 65535) {
        return false;
    }

    // Convert elements in-place from Big-Endian to Host format
    for (size_t i = 0; i < 65535; ++i) {
        out_array[i] = big_endian_to_host(out_array[i]);
    }

    return true;
}
void mem_write(uint32_t address, uint32_t val) {
    memory[address] = val;
}

uint32_t mem_read(uint32_t address) {
    if (address == MR_KBSR) {
        if (check_key()) {
            memory[MR_KBSR] = (1 << 31);
            memory[MR_KBDR] = getchar();
        } else {
            memory[MR_KBSR] = 0;
        }
    }
    return memory[address];
}


int main(int argc, const char* argv[]) {
    if (argc < 2)
    {
        /* show usage string */
        printf("need file to execute ...\n");
        exit(2);
    }
    
    for (int j = 1; j < argc; ++j)
    {
        if (!read_image(argv[j]))
        {
            printf("failed to load image: %s\n", argv[j]);
            exit(1);
        }
    }

    // mem_write(0,0xD0000023);

    signal(SIGINT, handle_interrupt);
    disable_input_buffering();

    /* since exactly one condition flag should be set at any given time, set the Z flag */
    reg[R_COND] = FL_ZRO;

    /* set the PC to starting position */
    reg[R_PC] = 0;

    int running = 1;
    while (running) {
        /* FETCH */
        uint32_t instr = mem_read(reg[R_PC]++);
        uint32_t op = instr >> 28;


        switch (op) {
            case OP_LD:
                {
                    uint32_t r0 = (instr >> 25) & 0x7;
                    uint32_t imm_flag = (instr >> 23) & 0x3;
                    if (imm_flag == 0x0) { // reg
                        uint32_t r1 = (instr >> 20) & 0x3;
                        reg[r0] = reg[r1];
                    } else if (imm_flag == 0x1) { // mem
                        uint32_t addr = (instr & 0xFFFF);
                        reg[r0] = mem_read(addr);
                    } else { // imm
                        uint32_t imm = (instr & 0xFFFF);
                        reg[r0] = imm;
                    }
                }
                break;
            case OP_ST:
                {
                    uint32_t r0 = (instr >> 25) & 0x7;
                    uint32_t addr = (instr & 0xFFFF);
                    mem_write(addr,reg[r0]);
                }
                break;
            case OP_ADD:
                {
                    uint32_t r0 = (instr >> 25) & 0x7;
                    uint32_t r1 = (instr >> 22) & 0x7;
                    uint32_t imm_flag = (instr >> 21) & 0x1;
                
                    if (imm_flag) {
                        uint32_t imm5 = sign_extend(instr & 0xFFFF, 16);
                        reg[r0] = reg[r1] + imm5;
                    } else {
                        uint32_t r2 = instr & 0x7;
                        reg[r0] = reg[r1] + reg[r2];
                    }
                
                    update_flags(r0);
                }
                break;
            case OP_SUB:
                {
                    uint32_t r0 = (instr >> 25) & 0x7;
                    uint32_t r1 = (instr >> 22) & 0x7;
                    uint32_t imm_flag = (instr >> 21) & 0x1;
                
                    if (imm_flag) {
                        uint32_t imm5 = sign_extend(instr & 0xFFFF, 16);
                        reg[r0] = reg[r1] - imm5;
                    } else {
                        uint32_t r2 = instr & 0x7;
                        reg[r0] = reg[r1] - reg[r2];
                    }
                
                    update_flags(r0);
                }
                break;
            case OP_AND:
                {
                    uint32_t r0 = (instr >> 25) & 0x7;
                    uint32_t r1 = (instr >> 22) & 0x7;
                    uint32_t imm_flag = (instr >> 21) & 0x1;
                
                    if (imm_flag) {
                        uint32_t imm5 = sign_extend(instr & 0xFFFF, 16);
                        reg[r0] = reg[r1] & imm5;
                    } else {
                        uint16_t r2 = instr & 0x7;
                        reg[r0] = reg[r1] & reg[r2];
                    }
                    update_flags(r0);
                }
                break;
            case OP_OR:
                {
                    uint32_t r0 = (instr >> 25) & 0x7;
                    uint32_t r1 = (instr >> 22) & 0x7;
                    uint32_t imm_flag = (instr >> 21) & 0x1;
                
                    if (imm_flag) {
                        uint32_t imm5 = sign_extend(instr & 0xFFFF, 16);
                        reg[r0] = reg[r1] | imm5;
                    } else {
                        uint16_t r2 = instr & 0x7;
                        reg[r0] = reg[r1] | reg[r2];
                    }
                    update_flags(r0);
                }
                break;
            case OP_SHL:
                {
                    uint32_t r0 = (instr >> 25) & 0x7;
                    uint32_t r1 = (instr >> 22) & 0x7;
                    uint32_t imm_flag = (instr >> 21) & 0x1;
                
                    if (imm_flag) {
                        uint32_t imm5 = sign_extend(instr & 0xFFFF, 16);
                        reg[r0] = reg[r1] << imm5;
                    } else {
                        uint16_t r2 = instr & 0x7;
                        reg[r0] = reg[r1] << reg[r2];
                    }
                    update_flags(r0);
                }
                break;
            case OP_SHR:
                {
                    uint32_t r0 = (instr >> 25) & 0x7;
                    uint32_t r1 = (instr >> 22) & 0x7;
                    uint32_t imm_flag = (instr >> 21) & 0x1;
                
                    if (imm_flag) {
                        uint32_t imm5 = sign_extend(instr & 0xFFFF, 16);
                        reg[r0] = reg[r1] >> imm5;
                    } else {
                        uint16_t r2 = instr & 0x7;
                        reg[r0] = reg[r1] >> reg[r2];
                    }
                    update_flags(r0);
                }
                break;
            case OP_NOT:
                {
                    uint32_t r0 = (instr >> 25) & 0x7;
                    uint16_t r1 = (instr >> 22) & 0x7;
                
                    reg[r0] = ~reg[r1];
                    update_flags(r0);
                }
                break;
            case OP_BR:
                {
                    uint32_t cond_flag = (instr >> 25) & 0x7;
                    uint32_t imm_flag = (instr >> 24) & 0x1;
                    if (cond_flag & reg[R_COND]) {
                        if (imm_flag) {
                            uint32_t addr = sign_extend(instr & 0xFFFF, 16);
                            reg[R_PC] = addr;
                        } else {
                            uint32_t r1 = (instr >> 21) & 0x7;
                            reg[R_PC] = reg[r1];
                        }
                    }
                }
                break;
            case OP_JMP:
                {
                    /* Also handles RET */
                    uint32_t imm_flag = (instr >> 27) & 0x1;
                    if (imm_flag) {
                        reg[R_PC] = instr & 0xFFFF;
                    } else {                
                        uint32_t r1 = (instr >> 24) & 0x7;
                        reg[R_PC] = reg[r1];
                    }
                }
                break;
            case OP_LDR:
                {
                    uint32_t r0 = (instr >> 25) & 0x7;
                    uint32_t r1 = (instr >> 22) & 0x7;
                    reg[r0] = mem_read(reg[r1]);
                    update_flags(r0);
                }
                break;
            case OP_STR:
                {
                    uint32_t r0 = (instr >> 25) & 0x7;
                    uint32_t r1 = (instr >> 22) & 0x7;
                    mem_write(reg[r0], reg[r1]);
                }
                break;
            case OP_TRAP:
                reg[R_R7] = reg[R_PC];
                
                switch (instr & 0xFF)
                {
                    case TRAP_GETC:
                        /* read a single ASCII char */
                        reg[R_R0] = (uint32_t)getchar();
                        update_flags(R_R0);
                        break;
                    case TRAP_OUT:
                        putc((char)reg[R_R0], stdout);
                        fflush(stdout);
                        break;
                    case TRAP_PUTS:
                        {
                            /* one char per word */
                            uint32_t* c = memory + reg[R_R0];
                            while (*c) {
                                putc((char)*c, stdout);
                                ++c;
                            }
                            fflush(stdout);
                        }
                        break;
                    case TRAP_IN:
                        {
                            printf("Enter a character: ");
                            char c = getchar();
                            putc(c, stdout);
                            fflush(stdout);
                            reg[R_R0] = (uint32_t)c;
                            update_flags(R_R0);
                        }
                        break;
                    case TRAP_HALT:
                        puts("HALT");
                        fflush(stdout);
                        running = 0;
                        break;
                }
                break;
            case OP_NOP:
                break;
            default:
                abort();
                break;
        }
    }
    restore_input_buffering();
}