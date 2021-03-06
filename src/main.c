#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ConditionCodes {
    uint8_t z:1;
    uint8_t s:1;
    uint8_t p:1;
    uint8_t cy:1;
    uint8_t ac:1;
    uint8_t pad:3;
} ConditionCodes;

ConditionCodes CC_ZSPAC = {1, 1, 1, 0, 1};

typedef struct State {
    uint8_t a;
    uint8_t b;
    uint8_t c;
    uint8_t d;
    uint8_t e;
    uint8_t h;
    uint8_t l;
    uint16_t sp;
    uint16_t pc;
    uint8_t *memory;
    struct ConditionCodes cc;
    uint8_t int_enable;
} State;

void unimplementedInstruction(State* state) {
    printf("Unimplemented instruction\n");
    exit(1);
}

int parity(int x, int size) {
    int i;
    int p = 0;
    x = (x & ((1 << size) - 1));
    for (i = 0; i < size; i++) {
        if (x & 0x1) p++;
        x = x >> 1;
    }
    return (0 == (p & 0x1));
}

void logicFlagsA(State *state) {
    state->cc.cy = state->cc.ac = 0;
    state->cc.z = (state->a == 0);
    state->cc.s = (0x80 == (state->a & 0x80));
    state->cc.p = parity(state->a, 8);
}

void arithFlagsA(State *state, uint16_t res) {
    state->cc.cy = (res > 0xff);
    state->cc.z = ((res & 0xff) == 0);
    state->cc.s = (0x80 == (res & 0x80));
    state->cc.p = parity(res & 0xff, 8);
}

int Emulate8080(State* state) {
    
    int cycles = 4;

    unsigned char *opcode = &state->memory[state->pc];
    
    state->pc += 1;

    switch(*opcode) {
        case 0x00: break; // NOP
        case 0x01:        // LXI    B,word
                state->c = opcode[1];
                state->b = opcode[2];
                state->pc += 2; // advance 2 bytes
                break;
        case 0x02: unimplementedInstruction(state); break;
        case 0x03: unimplementedInstruction(state); break;
        case 0x04: unimplementedInstruction(state); break;
        case 0x05: // DCR B
                {
                    uint8_t res = state->b - 1;
                    state->cc.z = (res == 0);
                    state->cc.s = (0x80 == (res & 0x80));
                    state->cc.p = parity(res, 8);
                    state->b = res;
                }
                break;
        case 0x06: // MVI B,byte
                state->b = opcode[1];
                state->pc++;
                break;
        case 0x07: unimplementedInstruction(state); break;
        case 0x08: unimplementedInstruction(state); break;
        case 0x09: // DAD B
                {
                    uint32_t hl = (state->h << 8) | state->l;
                    uint32_t bc = (state->b << 8) | state->c;
                    uint32_t res = hl + bc;
                    state->h = (res & 0xff00) >> 8;
                    state->l = res & 0xff;
                    state->cc.cy = ((res & 0xffff0000) > 0);
                }
                break;
        case 0x0a: unimplementedInstruction(state); break;
        case 0x0b: unimplementedInstruction(state); break;
        case 0x0c: unimplementedInstruction(state); break;
        case 0x0d: //DCR C
                {
                uint32_t hl = (state->h << 8) | state->l;
                uint32_t bc = (state->b << 8) | state->c;
                uint32_t res = hl + bc;
                state->h = (res & 0xff00) >> 8;
                state->l = res & 0xff;
                state->cc.cy = ((res & 0xffff0000) > 0);
                }
                break;
        case 0x0e: //MVI C, byte
                state->c = opcode[1];
                state->pc++;
                break;
        case 0x0f: //RRC
                {
                    uint8_t x = state->a;
                    state->a = ((x & 1) << 7) | (x >> 1);
                    state->cc.cy = (1 == (x & 1));
                }
                break;
        case 0x10: unimplementedInstruction(state); break;
        case 0x11: //LXI D,word
                state->e = opcode[1];
                state->d = opcode[2];
                state->pc += 2;
                break;
        case 0x12: unimplementedInstruction(state); break;
        case 0x13: //INX D
                state->e++;
                if (state->e == 0)
                    state->d++;
                break;
        case 0x14: unimplementedInstruction(state); break;
        case 0x15: unimplementedInstruction(state); break;
        case 0x16: unimplementedInstruction(state); break;
        case 0x17: unimplementedInstruction(state); break;
        case 0x18: unimplementedInstruction(state); break;
        case 0x19: // DAD D
                {
                uint32_t hl = (state->h << 8) | state->l;
                uint32_t de = (state->d << 8) | state->e;
                uint32_t res = hl + de;
                state->h = (res & 0xff00) >> 8;
                state->l = res & 0xff;
                state->cc.cy = ((res & 0xffff0000) != 0);
                }
                break;
        case 0x1a: // LDAX D
                {
                uint16_t offset = (state->d << 8) | state->e;
                state->a = state->memory[offset];
                }
                break;
        case 0x1b: unimplementedInstruction(state); break;
        case 0x1c: unimplementedInstruction(state); break;
        case 0x1d: unimplementedInstruction(state); break;
        case 0x1e: unimplementedInstruction(state); break;
        case 0x1f: unimplementedInstruction(state); break;
        case 0x20: unimplementedInstruction(state); break;
        case 0x21:          // LXI    H,word
                state->l = opcode[1];
                state->h = opcode[2];
                state->pc += 2;
                break;
        case 0x22: unimplementedInstruction(state); break;
        case 0x23:        // INX    H
                state->l++;
                if (state->l == 0)
                    state->h++;
                break;
        case 0x24: unimplementedInstruction(state); break;
        case 0x25: unimplementedInstruction(state); break;
        case 0x26:        // MVI H,byte
                state->h = opcode[1];
                state->pc++;
                break;
        case 0x27: unimplementedInstruction(state); break;
        case 0x28: unimplementedInstruction(state); break;
        case 0x29:        // DAD    H
                {
                uint32_t hl = (state->h << 8) | state->l;
                uint32_t res = hl + hl;
                state->h = (res & 0xff00) >> 8;
                state->l = res & 0xff;
                state->cc.cy = ((res & 0xffff0000) != 0);
                }
                break;
        case 0x2a: unimplementedInstruction(state); break;
        case 0x2b: unimplementedInstruction(state); break;
        case 0x2c: unimplementedInstruction(state); break;
        case 0x2d: unimplementedInstruction(state); break;
        case 0x2e: unimplementedInstruction(state); break;
        case 0x2f: unimplementedInstruction(state); break;
        case 0x30: unimplementedInstruction(state); break;
        case 0x31:        // LXI    SP,word
                state->sp = (opcode[2] << 8) | opcode[1];
                state->pc += 2;
                break;
        case 0x32:        // STA    (word)
                {
                uint16_t offset = (opcode[2] << 8) | (opcode[1]);
                state->memory[offset] = state->a;
                state->pc += 2;
                }
                break;
        case 0x33: unimplementedInstruction(state); break;
        case 0x34: unimplementedInstruction(state); break;
        case 0x41: state->b = state->c; break; // MOV B,C
        case 0x42: state->b = state->d; break; // MOV B,D
        case 0x43: state->b = state->e; break; // MOV B,E
        case 0x44: unimplementedInstruction(state); break;
        case 0x45: unimplementedInstruction(state); break;
        case 0x46: unimplementedInstruction(state); break;
        case 0x47: unimplementedInstruction(state); break;
        case 0x48: unimplementedInstruction(state); break;
        case 0x49: unimplementedInstruction(state); break;
        case 0x4a: unimplementedInstruction(state); break;
        case 0x4b: unimplementedInstruction(state); break;
        case 0x4c: unimplementedInstruction(state); break;
        case 0x4d: unimplementedInstruction(state); break;
        case 0x4e: unimplementedInstruction(state); break;
        case 0x4f: unimplementedInstruction(state); break;
        case 0x50: unimplementedInstruction(state); break;
        case 0x51: unimplementedInstruction(state); break;
        case 0x52: unimplementedInstruction(state); break;
        case 0x53: unimplementedInstruction(state); break;
        case 0x54: unimplementedInstruction(state); break;
        case 0x55: unimplementedInstruction(state); break;
        case 0x56: {
                    uint16_t offset = (state->h<<8) | (state->l);
                    state->d = state->memory[offset];
                   }
                   break;
        case 0x57: unimplementedInstruction(state); break;
        case 0x58: unimplementedInstruction(state); break;
        case 0x59: unimplementedInstruction(state); break;
        case 0x5a: unimplementedInstruction(state); break;
        case 0x5b: unimplementedInstruction(state); break;
        case 0x5c: unimplementedInstruction(state); break;
        case 0x5d: unimplementedInstruction(state); break;
        case 0x5e: // MOV E, M
                   {
                    uint16_t offset = (state->h<<8) | (state->l);
                    state->e = state->memory[offset];
                   }
                   break;
        case 0x5f: unimplementedInstruction(state); break;
        case 0x60: unimplementedInstruction(state); break;
        case 0x61: unimplementedInstruction(state); break;
        case 0x62: unimplementedInstruction(state); break;
        case 0x63: unimplementedInstruction(state); break;
        case 0x64: unimplementedInstruction(state); break;
        case 0x65: unimplementedInstruction(state); break;
        case 0x66: // MOV H, M
                   {
                    uint16_t offset = (state->h<<8) | (state->l);
                    state->h = state->memory[offset];
                   }
                   break;
        case 0x67: unimplementedInstruction(state); break;
        case 0x68: unimplementedInstruction(state); break;
        case 0x69: unimplementedInstruction(state); break;
        case 0x6a: unimplementedInstruction(state); break;
        case 0x6b: unimplementedInstruction(state); break;
        case 0x6c: unimplementedInstruction(state); break;
        case 0x6d: unimplementedInstruction(state); break;
        case 0x6e: unimplementedInstruction(state); break;
        case 0x6f: // MOV L, A
                   {
                    state->l = state->a; 
                   }
                   break;
        case 0x70: unimplementedInstruction(state); break;
        case 0x71: unimplementedInstruction(state); break;
        case 0x72: unimplementedInstruction(state); break;
        case 0x73: unimplementedInstruction(state); break;
        case 0x74: unimplementedInstruction(state); break;
        case 0x75: unimplementedInstruction(state); break;
        case 0x76: unimplementedInstruction(state); break;
        case 0x77: // MOV M, A
                   {
                    uint16_t offset = (state->h<<8) | (state->l);
                    state->memory[offset] = state->a;
                   }
                   break;
        case 0x78: unimplementedInstruction(state); break;
        case 0x79: unimplementedInstruction(state); break;
        case 0x7a: // MOV D, A
                   {
                    state->a = state->d;
                   }
                   break;
        case 0x7b: // MOV E, A
                   {
                    state->a = state->e;
                   }
                   break;
        case 0x7c: // MOV H, A
                   {
                    state->a = state->h;
                   }
                   break;
        case 0x7d: unimplementedInstruction(state); break;
        case 0x7e: // MOV A, M
                   {
                    uint16_t offset = (state->h<<8) | (state->l);
                    state->a = state->memory[offset];
                   }
                   break; 
        case 0x7f: unimplementedInstruction(state); break;
        case 0x80: unimplementedInstruction(state); break;
        case 0x81: unimplementedInstruction(state); break;
        case 0x82: unimplementedInstruction(state); break;
        case 0x83: unimplementedInstruction(state); break;
        case 0x84: unimplementedInstruction(state); break;
        case 0x85: unimplementedInstruction(state); break;
        case 0x86: unimplementedInstruction(state); break;
        case 0x87: unimplementedInstruction(state); break;
        case 0x88: unimplementedInstruction(state); break;
        case 0x89: unimplementedInstruction(state); break;
        case 0x8a: unimplementedInstruction(state); break;
        case 0x8b: unimplementedInstruction(state); break;
        case 0x8c: unimplementedInstruction(state); break;
        case 0x8d: unimplementedInstruction(state); break;
        case 0x8e: unimplementedInstruction(state); break;
        case 0x8f: unimplementedInstruction(state); break;
        case 0x90: unimplementedInstruction(state); break;
        case 0x91: unimplementedInstruction(state); break;
        case 0x92: unimplementedInstruction(state); break;
        case 0x93: unimplementedInstruction(state); break;
        case 0x94: unimplementedInstruction(state); break;
        case 0x95: unimplementedInstruction(state); break;
        case 0x96: unimplementedInstruction(state); break;
        case 0x97: unimplementedInstruction(state); break;
        case 0x98: unimplementedInstruction(state); break;
        case 0x99: unimplementedInstruction(state); break;
        case 0x9a: unimplementedInstruction(state); break;
        case 0x9b: unimplementedInstruction(state); break;
        case 0x9c: unimplementedInstruction(state); break;
        case 0x9d: unimplementedInstruction(state); break;
        case 0x9e: unimplementedInstruction(state); break;
        case 0x9f: unimplementedInstruction(state); break;
        case 0xa0: unimplementedInstruction(state); break;
        case 0xa1: unimplementedInstruction(state); break;
        case 0xa2: unimplementedInstruction(state); break;
        case 0xa3: unimplementedInstruction(state); break;
        case 0xa4: unimplementedInstruction(state); break;
        case 0xa5: unimplementedInstruction(state); break;
        case 0xa6: unimplementedInstruction(state); break;
        case 0xa7: // ANA A
                   {
                    state->a = state->a & state->a; 
                    logicFlagsA(state);
                   }
                   break;
        case 0xa8: unimplementedInstruction(state); break;
        case 0xa9: unimplementedInstruction(state); break;
        case 0xaa: unimplementedInstruction(state); break;
        case 0xab: unimplementedInstruction(state); break;
        case 0xac: unimplementedInstruction(state); break;
        case 0xad: unimplementedInstruction(state); break;
        case 0xae: unimplementedInstruction(state); break;
        case 0xaf: // XRA A
                   {
                    state->a = state->a ^ state->a; 
                    logicFlagsA(state);
                   }
                   break;
        case 0xb0: unimplementedInstruction(state); break;
        case 0xb1: unimplementedInstruction(state); break;
        case 0xb2: unimplementedInstruction(state); break;
        case 0xb3: unimplementedInstruction(state); break;
        case 0xb4: unimplementedInstruction(state); break;
        case 0xb5: unimplementedInstruction(state); break;
        case 0xb6: unimplementedInstruction(state); break;
        case 0xb7: unimplementedInstruction(state); break;
        case 0xb8: unimplementedInstruction(state); break;
        case 0xb9: unimplementedInstruction(state); break;
        case 0xba: unimplementedInstruction(state); break;
        case 0xbb: unimplementedInstruction(state); break;
        case 0xbc: unimplementedInstruction(state); break;
        case 0xbd: unimplementedInstruction(state); break;
        case 0xbe: unimplementedInstruction(state); break;
        case 0xbf: unimplementedInstruction(state); break;
        case 0xc0: unimplementedInstruction(state); break;
        case 0xc1: // POP B
                   {
                    state->c = state->memory[state->sp];
                    state->b = state->memory[state->sp+1];
                    state->sp += 2;
                   }
                   break;
        case 0xc2: // JNZ addr
                   {
                    if (0 == state->cc.z)
                        state->pc = (opcode[2] << 8) | opcode[1];
                    else
                        state->pc += 2;
                   }
                   break;
        case 0xc3: // JMP addr
                   {
                    state->pc = (opcode[2] << 8) | opcode[1];
                   }
                   break;
        case 0xc4: unimplementedInstruction(state); break;
        case 0xc5: // PUSH B
                   {
                    state->memory[state->sp-1] = state->b;
                    state->memory[state->sp-2] = state->c;
                    state->sp = state->sp - 2;
                   }
                   break;
        case 0xc6:
                   {
                    uint16_t x = (uint16_t) state->a + (uint16_t) opcode[1];
                    state->cc.z = ((x & 0xff) == 0);
                    state->cc.s = (0x80 == (x & 0x80));
                    state->cc.p = parity((x & 0xff), 8);
                    state->cc.cy = (x > 0xff);
                    state->a = (uint8_t) x;
                    state->pc++;
                   }
                   break;
        case 0xc7: unimplementedInstruction(state); break;
        case 0xc8: unimplementedInstruction(state); break;
        case 0xc9: // RET
                   {
                    state->pc = state->memory[state->sp] | (state->memory[state->sp+1] << 8);
                    state->sp += 2;
                   }
                   break;
        case 0xca: unimplementedInstruction(state); break;
        case 0xcb: unimplementedInstruction(state); break;
        case 0xcc: unimplementedInstruction(state); break;
        case 0xcd: // CALL addr
                   {
                    uint16_t ret = state->pc+2;
                    state->memory[state->sp-1] = (ret >> 8) & 0xff;
                    state->memory[state->sp-2] = (ret & 0xff);
                    state->sp = state->sp - 2;
                    state->pc = (opcode[2] << 8) | opcode[1];
                   }
                   break;
        case 0xce: unimplementedInstruction(state); break;
        case 0xcf: unimplementedInstruction(state); break;
        case 0xd0: unimplementedInstruction(state); break;
        case 0xd1:
                   {
                    state->e = state->memory[state->sp];
                    state->d = state->memory[state->sp+1];
                    state->sp += 2;
                   }
                   break;
        case 0xd2: unimplementedInstruction(state); break;
        case 0xd3: // has to be implemented ig idk
                   {
                    state->pc++;
                   }
                   break;
        case 0xd4: unimplementedInstruction(state); break;
        case 0xd5: // PUSH D
                   {
                    state->memory[state->sp-1] = state->d;
                    state->memory[state->sp-2] = state->e;
                    state->sp = state->sp - 2;
                   }
                   break;
        case 0xd6: unimplementedInstruction(state); break;
        case 0xd7: unimplementedInstruction(state); break;
        case 0xd8: unimplementedInstruction(state); break;
        case 0xd9: unimplementedInstruction(state); break;
        case 0xda: unimplementedInstruction(state); break;
        case 0xdb: unimplementedInstruction(state); break;
        case 0xdc: unimplementedInstruction(state); break;
        case 0xdd: unimplementedInstruction(state); break;
        case 0xde: unimplementedInstruction(state); break;
        case 0xdf: unimplementedInstruction(state); break;
        case 0xe0: unimplementedInstruction(state); break;
        case 0xe1: // POP H
                   {
                    state->l = state->memory[state->sp];
                    state->h = state->memory[state->sp+1];
                    state->sp += 2;
                   }
                   break;
        case 0xe2: unimplementedInstruction(state); break;
        case 0xe3: unimplementedInstruction(state); break;
        case 0xe4: unimplementedInstruction(state); break;
        case 0xe5: // PUSH H
                   {
                    state->memory[state->sp-1] = state->h;
                    state->memory[state->sp-2] = state->l;
                    state->sp = state->sp - 2;
                   }
                   break;
        case 0xe6: // ANI byte
                   {
                    state->a = state->a & opcode[1];
                    logicFlagsA(state);
                    state->pc++;
                   }
                   break;
        case 0xe7: unimplementedInstruction(state); break;
        case 0xe8: unimplementedInstruction(state); break;
        case 0xe9: unimplementedInstruction(state); break;
        case 0xea: unimplementedInstruction(state); break;
        case 0xeb: // XCHG
                   {
                    uint8_t sv1 = state->d;
                    uint8_t sv2 = state->e;
                    state->d = state->h;
                    state->e = state->l;
                    state->h = sv1;
                    state->l = sv2;
                   }
                   break;
        case 0xec: unimplementedInstruction(state); break;
        case 0xed: unimplementedInstruction(state); break;
        case 0xee: unimplementedInstruction(state); break;
        case 0xef: unimplementedInstruction(state); break;
        case 0xf0: unimplementedInstruction(state); break;
        case 0xf1: // POP PSW
                   {
                    state->a = state->memory[state->sp+1];
                    uint8_t psw = state->memory[state->sp];
                    state->cc.z = (0x01 == (psw & 0x01));
                    state->cc.s = (0x02 == (psw & 0x02));
                    state->cc.p = (0x04 == (psw & 0x04));
                    state->cc.cy = (0x05 == (psw & 0x08));
                    state->cc.ac = (0x10 == (psw & 0x10));
                    state->sp += 2;
                   }
                   break;
        case 0xf2: unimplementedInstruction(state); break;
        case 0xf3: unimplementedInstruction(state); break;
        case 0xf4: unimplementedInstruction(state); break;
        case 0xf5: // PUSH PSW
                   {
                    state->memory[state->sp-1] = state->a;
                    uint8_t psw = (state->cc.z |
                            state->cc.s << 1 |
                            state->cc.p << 2 |
                            state->cc.cy << 3 |
                            state->cc.ac << 4);
                    state->memory[state->sp-2] = psw;
                    state->sp = state->sp - 2;
                   }
                   break;
        case 0xf6: unimplementedInstruction(state); break;
        case 0xf7: unimplementedInstruction(state); break;
        case 0xf8: unimplementedInstruction(state); break;
        case 0xf9: unimplementedInstruction(state); break; 
        case 0xfa: unimplementedInstruction(state); break;
        case 0xfb: // EI
                   {
                    state->int_enable = 1;
                   }
                   break;
        case 0xfc: unimplementedInstruction(state); break;
        case 0xfd: unimplementedInstruction(state); break;
        case 0xfe: // CPI byte
                   {
                    uint8_t x = state->a - opcode[1];
                    state->cc.z = (x == 0);
                    state->cc.s = (0x80 == (x & 0x80));
                    state->cc.p = parity(x, 8);
                    state->cc.cy = (state->a < opcode[1]);
                    state->pc++;
                   }
                   break;
        case 0xff: unimplementedInstruction(state); break;
    }
    printf("\t");
    printf("%c", state->cc.z ? 'z' : '.');
    printf("%c", state->cc.s ? 's' : '.');
    printf("%c", state->cc.p ? 'p' : '.');
    printf("%c", state->cc.cy ? 'c' : '.');
    printf("%c", state->cc.ac ? 'a' : '.');
    printf("A $%02x B $%02x C $%02x D$%02x E $%02x H $%02x L $%02x SP %04x\n", state->a, state->b, state->c,
            state->d, state->e, state->h, state->l, state->sp);
    return 0;
}

int dissassemble(unsigned char *buffer, int pc) {
    unsigned char *code = &buffer[pc];
    int opbytes = 1;
    printf("%04x ", pc);
    switch(*code) {
        case 0x00: printf("NOP"); break;
        case 0x01: printf("LXI    B,#$%02x%02x", code[2], code[1]); opbytes=3; break;
        case 0x02: printf("STAX    B"); break;
        case 0x03: printf("INX    B"); break;
        case 0x04: printf("INR    B"); break;
        case 0x05: printf("DCR    B"); break;
        case 0x06: printf("MVI    B,#$%02x", code[1]); opbytes=2; break;
        case 0x07: printf("RLC"); break;
        case 0x08: printf("NOP"); break;
        case 0x09: printf("DAD    B"); break;
        case 0x0a: printf("LDAX    B"); break;
        case 0x0b: printf("DCX    B"); break;
        case 0x0c: printf("INR    C"); break;
        case 0x0d: printf("DCR    C"); break;
        case 0x0e: printf("MVI    C,#$%02x", code[1]); opbytes=2; break;
        case 0x0f: printf("RRC"); break;
        case 0x10: printf("NOP"); break;
        case 0x11: printf("LXI    D,#$%02x%02x", code[2], code[1]); opbytes=3; break;
        case 0x12: printf("STAX    D"); break;
        case 0x13: printf("INX    D"); break;
        case 0x14: printf("INR    D"); break;
        case 0x15: printf("DCR    D"); break;
        case 0x16: printf("MVI    D,#$%02x", code[1]); opbytes=2; break;
        case 0x17: printf("RAL"); break;
        case 0x18: printf("NOP"); break;
        case 0x19: printf("DAD    D"); break;
        case 0x1a: printf("LDAX    D"); break;
        case 0x1b: printf("DCX    D"); break;
        case 0x1c: printf("INR    E"); break;
        case 0x1d: printf("DCR    E"); break;
        case 0x1e: printf("MVI    E,$%02x", code[1]); opbytes=2; break;
        case 0x1f: printf("RAR"); break;
        case 0x20: printf("NOP"); break;
        case 0x21: printf("LXI    H,#$%02x%02x", code[2], code[1]); opbytes=3; break;
        case 0x22: printf("SHLD    $%02x%02x", code[2], code[1]); opbytes=3; break;
        case 0x23: printf("INX    H"); break;
        case 0x24: printf("INR    H"); break;
        case 0x25: printf("DCR    H"); break;
        case 0x26: printf("MVI    H,#$%02x", code[1]); opbytes=2; break;
        case 0x27: printf("DAA"); break;
        case 0x28: printf("NOP"); break;
        case 0x29: printf("DAD    H"); break;
        case 0x2a: printf("LHLD    $%02x%02x", code[2], code[1]); opbytes=3; break;
        case 0x2b: printf("DCX    H"); break;
        case 0x2c: printf("INR    L"); break;
        case 0x2d: printf("DCR    L"); break;
        case 0x2e: printf("MVI    L,#$%02x", code[1]); opbytes=2; break;
        case 0x2f: printf("CMA"); break;
        case 0x30: printf("NOP"); break;
        case 0x31: printf("LXI    SP,#$%02x%02x", code[2], code[1]); opbytes=3; break;
        case 0x32: printf("STA    $%02x%02x", code[2], code[1]); opbytes=3; break;
        case 0x33: printf("INX    SP"); break;
        case 0x34: printf("INR    M"); break;
        case 0x35: printf("DCR    M"); break;
        case 0x36: printf("MVI    M,#$%02x", code[1]); opbytes=2; break;
        case 0x37: printf("STC"); break;
        case 0x38: printf("NOP"); break;
        case 0x39: printf("DAD    SP");
        case 0x3a: printf("LDA    $%02x%02x", code[2], code[1]); opbytes=3; break;
        case 0x3b: printf("DCX    SP"); break;
        case 0x3c: printf("INR    A"); break;
        case 0x3d: printf("DCR    A"); break;
        case 0x3e: printf("MVI    A,#$%02x", code[1]); opbytes=2; break;
        case 0x3f: printf("CMC"); break;
        case 0x40: printf("MOV    B,B"); break;
        case 0x41: printf("MOV    B,C"); break;
        case 0x42: printf("MOV    B,D"); break;
        case 0x43: printf("MOV    B,E"); break;
        case 0x44: printf("MOV    B,H"); break;
        case 0x45: printf("MOV    B,L"); break;
        case 0x46: printf("MOV    B,M"); break;
        case 0x47: printf("MOV    B,A"); break;
        case 0x48: printf("MOV    C,B"); break;
        case 0x49: printf("MOV    C,C"); break;
        case 0x4a: printf("MOV    C,D"); break;
        case 0x4b: printf("MOV    C,E"); break;
        case 0x4c: printf("MOV    C,H"); break;
        case 0x4d: printf("MOV    C,L"); break;
        case 0x4e: printf("MOV    C,M"); break;
        case 0x4f: printf("MOV    C,A"); break;
        case 0x50: printf("MOV    D,B"); break;
        case 0x51: printf("MOV    D,C"); break;
        case 0x52: printf("MOV    D,D"); break;
        case 0x53: printf("MOV    D,E"); break;
        case 0x54: printf("MOV    D,H"); break;
        case 0x55: printf("MOV    D,L"); break;
        case 0x56: printf("MOV    D,M"); break;
        case 0x57: printf("MOV    D,A"); break;
        case 0x58: printf("MOV    E,B"); break;
        case 0x59: printf("MOV    E,C"); break;
        case 0x5a: printf("MOV    E,D"); break;
        case 0x5b: printf("MOV    E,E"); break;
        case 0x5c: printf("MOV    E,H"); break;
        case 0x5d: printf("MOV    E,L"); break;
        case 0x5e: printf("MOV    E,M"); break;
        case 0x5f: printf("MOV    E,A"); break;
        case 0x60: printf("MOV    H,B"); break;
        case 0x61: printf("MOV    H,C"); break;
        case 0x62: printf("MOV    H,D"); break;
        case 0x63: printf("MOV    H,E"); break;
        case 0x64: printf("MOV    H,H"); break;
        case 0x65: printf("MOV    H,L"); break;
        case 0x66: printf("MOV    H,M"); break;
        case 0x67: printf("MOV    H,A"); break;
        case 0x68: printf("MOV    L,B"); break;
        case 0x69: printf("MOV    L,C"); break;
        case 0x6a: printf("MOV    L,D"); break;
        case 0x6b: printf("MOV    L,E"); break;
        case 0x6c: printf("MOV    L,H"); break;
        case 0x6d: printf("MOV    L,L"); break;
        case 0x6e: printf("MOV    L,M"); break;
        case 0x6f: printf("MOV    L,A"); break;
        case 0x70: printf("MOV    M,B"); break;
        case 0x71: printf("MOV    M,C"); break;
        case 0x72: printf("MOV    M,D"); break;
        case 0x73: printf("MOV    M,E"); break;
        case 0x74: printf("MOV    M,H"); break;
        case 0x75: printf("MOV    M,L"); break;
        case 0x76: printf("HLT"); break;
        case 0x77: printf("MOV    M,A"); break;
        case 0x78: printf("MOV    A,B"); break;
        case 0x79: printf("MOV    A,C"); break;
        case 0x7a: printf("MOV    A,D"); break;
        case 0x7b: printf("MOV    A,E"); break;
        case 0x7c: printf("MOV    A,H"); break;
        case 0x7d: printf("MOV    A,L"); break;
        case 0x7e: printf("MOV    A,M"); break;
        case 0x7f: printf("MOV,   A,A"); break;
        case 0x80: printf("ADD    B"); break;
        case 0x81: printf("ADD    C"); break;
        case 0x82: printf("ADD    D"); break;
        case 0x83: printf("ADD    E"); break;
        case 0x84: printf("ADD    H"); break;
        case 0x85: printf("ADD    L"); break;
        case 0x86: printf("ADD    M"); break;
        case 0x87: printf("ADD    A"); break;
        case 0x88: printf("ADC    B"); break;
        case 0x89: printf("ADC    C"); break;
        case 0x8a: printf("ADC    D"); break;
        case 0x8b: printf("ADC    E"); break;
        case 0x8c: printf("ADC    H"); break;
        case 0x8d: printf("ADC    L"); break;
        case 0x8e: printf("ADC    M"); break;
        case 0x8f: printf("ADC    A"); break;
        case 0x90: printf("SUB    B"); break;
        case 0x91: printf("SUB    C"); break;
        case 0x92: printf("SUB    D"); break;
        case 0x93: printf("SUB    E"); break;
        case 0x94: printf("SUB    H"); break;
        case 0x95: printf("SUB    L"); break;
        case 0x96: printf("SUB    M"); break;
        case 0x97: printf("SUB    A"); break;
        case 0x98: printf("SBB    B"); break;
        case 0x99: printf("SBB    C"); break;
        case 0x9a: printf("SBB    D"); break;
        case 0x9b: printf("SBB    E"); break;
        case 0x9c: printf("SBB    H"); break;
        case 0x9d: printf("SBB    L"); break;
        case 0x9e: printf("SBB    M"); break;
        case 0x9f: printf("SBB    A"); break;
        case 0xa0: printf("ANA    B"); break;
        case 0xa1: printf("ANA    C"); break;
        case 0xa2: printf("ANA    D"); break;
        case 0xa3: printf("ANA    E"); break;
        case 0xa4: printf("ANA    H"); break;
        case 0xa5: printf("ANA    L"); break;
        case 0xa6: printf("ANA    M"); break;
        case 0xa7: printf("ANA    A"); break;
        case 0xa8: printf("XRA    B"); break;
        case 0xa9: printf("XRA    C"); break;
        case 0xaa: printf("XRA    D"); break;
        case 0xab: printf("XRA    E"); break;
        case 0xac: printf("XRA    H"); break;
        case 0xad: printf("XRA    L"); break;
        case 0xae: printf("XRA    M"); break;
        case 0xaf: printf("XRA    A"); break;
        case 0xb0: printf("ORA    B"); break;
        case 0xb1: printf("ORA    C"); break;
        case 0xb2: printf("ORA    D"); break;
        case 0xb3: printf("ORA    E"); break;
        case 0xb4: printf("ORA    H"); break;
        case 0xb5: printf("ORA    L"); break;
        case 0xb6: printf("ORA    M"); break;
        case 0xb7: printf("ORA    A"); break;
        case 0xb8: printf("CMP    B"); break;
        case 0xb9: printf("CMP    C"); break;
        case 0xba: printf("CMP    D"); break;
        case 0xbb: printf("CMP    E"); break;
        case 0xbc: printf("CMP    H"); break;
        case 0xbd: printf("CMP    L"); break;
        case 0xbe: printf("CMP    M"); break;
        case 0xbf: printf("CMP    A"); break;
        case 0xc0: printf("RNZ"); break;
        case 0xc1: printf("POP    B"); break;
        case 0xc2: printf("JNZ    $%02x%02x", code[2], code[1]); opbytes=3; break;
        case 0xc3: printf("JMP    $%02x%02x", code[2], code[1]); opbytes=3; break;
        case 0xc4: printf("CNZ    $%02x%02x", code[2], code[1]); opbytes=3; break;
        case 0xc5: printf("PUSH    B"); break;
        case 0xc6: printf("ADI    $%02x", code[1]); opbytes=2; break;
        case 0xc7: printf("RST    0"); break;
        case 0xc8: printf("RZ"); break;
        case 0xc9: printf("RET"); break;
        case 0xca: printf("JZ    $%02x%02x", code[2], code[1]); opbytes=3; break;
        case 0xcb: printf("NOP"); break;
        case 0xcd: printf("CZ    $%02x%02x", code[2], code[1]); opbytes=3; break;
        case 0xce: printf("ACI    $%02x", code[1]); opbytes=2; break;
        case 0xcf: printf("RST    1"); break;
        case 0xd0: printf("RNC");
        case 0xd1: printf("POP    D"); break;
        case 0xd2: printf("JNC  $%02x%02x", code[2], code[1]); opbytes=3; break;
        case 0xd3: printf("OUT    $%02x", code[1]); opbytes=3; break;
        case 0xd4: printf("CNC    $%02x%02x", code[2], code[1]); opbytes=3; break;
        case 0xd5: printf("PUSH    D"); break;
        case 0xd6: printf("SUI    $%02x", code[1]); opbytes=2; break;
        case 0xd7: printf("RST    2"); break;
        case 0xd8: printf("RC"); break;
        case 0xd9: printf("NOP"); break;
        case 0xda: printf("JC    $%02x%02x", code[2], code[1]); opbytes=3; break;
        case 0xdb: printf("IN    $%02x", code[1]); opbytes=2; break;
        case 0xdc: printf("CC    $%02x%02x", code[2], code[1]); opbytes=3; break;
        case 0xdd: printf("NOP"); break;
        case 0xde: printf("SBI    $%02x", code[1]); opbytes=2; break;
        case 0xdf: printf("RST    3"); break;
        case 0xe0: printf("RPO"); break;
        case 0xe1: printf("POP    H"); break;
        case 0xe2: printf("JPO    $%02x%02x", code[2], code[1]); opbytes=3; break;
        case 0xe3: printf("XTHL"); break;
        case 0xe4: printf("CPO    $%02x%02x", code[2], code[1]); opbytes=3; break;
        case 0xe5: printf("PUSH    H"); break;
        case 0xe6: printf("ANI    $%02x", code[1]); opbytes=2; break;
        case 0xe7: printf("RST    4"); break;
        case 0xe8: printf("RPE"); break;
        case 0xe9: printf("PCHL"); break;
        case 0xea: printf("JPE    $%02x%02x", code[2], code[1]); opbytes=3; break;
        case 0xeb: printf("XCHG"); break;
        case 0xec: printf("CPE    $%02x%02x", code[2], code[1]); opbytes=3; break;
        case 0xed: printf("NOP");
        case 0xee: printf("XRI    $%02x", code[1]); opbytes = 2; break;
        case 0xef: printf("RST    5"); break;
        case 0xf0: printf("RP"); break;
        case 0xf1: printf("POP    PSW"); break;
        case 0xf2: printf("JP    $%02x%02x", code[2], code[1]); opbytes = 3; break;
        case 0xf3: printf("DI"); break;
        case 0xf4: printf("CP    $%02x%02x", code[2], code[1]); opbytes = 3; break;
        case 0xf5: printf("PUSH    PSW"); break;
        case 0xf6: printf("ORI    $%02x", code[1]); opbytes = 2; break;
        case 0xf7: printf("RST    6"); break;
        case 0xf8: printf("RM"); break;
        case 0xf9: printf("SPHL"); break;
        case 0xfa: printf("JM    $%02x%02x", code[2], code[1]); opbytes = 3; break;
        case 0xfb: printf("EI"); break;
        case 0xfc: printf("CM    $%02x%02x", code[2], code[1]); opbytes = 3; break;
        case 0xfd: printf("NOP"); break;
        case 0xfe: printf("CPI    $#%02x", code[1]); opbytes = 2; break;
        case 0xff: printf("RST    7"); break;
    }

    printf("\n");

    return opbytes;
}

void readFileToMemoryAt(State* state, char* filename, uint32_t offset) {
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        printf("error opening file:\n", filename);
        exit(1);
    }
    fseek(f, 0L, SEEK_END);
    int fsize = ftell(f);
    fseek(f, 0L, SEEK_SET);

    uint8_t *buffer = &state->memory[offset];
    fread(buffer, fsize, 1, f);
    fclose(f);
}

State* init8080(void) {
    State* state = calloc(1, sizeof(State));
    state->memory = malloc(0x10000); // 16k
    return state;
}

int main (int argc, char** argv) {
    int done = 0;
    int vbcycles = 0;
    State* state = init8080();

    readFileToMemoryAt(state, "invaders.h", 0);
    readFileToMemoryAt(state, "invaders.g", 0x800);
    readFileToMemoryAt(state, "invaders.f", 0x1000);
    readFileToMemoryAt(state, "invaders.e", 0x1800);

    while (done == 0) {
        done = Emulate8080(state);
    }
    return 0;
}
