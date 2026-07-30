/*
 * SCL - CHIP-8 Emulator, Debugger, and Compiler
 * Copyright (c) 2026 Sean Cornell <secornell@ucsd.edu>
 * MIT License
 *
 * Intermediate representation for the compiler backend.
 *
 * The AST is lowered to a linear IR over unlimited VIRTUAL registers.
 * A linear-scan allocator maps virtual registers onto the 13 usable
 * physical registers (V1-VC, VE), spilling to memory when necessary,
 * and an emitter lowers the allocated IR to CHIP-8 bytecode.
 *
 * Register conventions:
 *   V0 - emitter scratch (memory transfers, parallel-move cycle breaking)
 *   VD - runtime call-stack offset (never allocated)
 *   VF - ALU flags (never allocated)
 *   VE - return value register (allocatable; the call sequence special-cases
 *        callers that keep it live across a call)
 *
 * Every IR instruction lowers to an ATOMIC bytecode sequence: spill code is
 * only ever inserted between IR instructions, so it can never split a
 * skip/jump pair or clobber VF between a SUB and its flag test.
 */

#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace chip8 {
namespace compiler {

using VReg = uint16_t;
constexpr VReg NO_VREG = 0xFFFF;

enum class IROp {
    // Register ops
    Const,      // dst = imm                          (6xkk)
    Move,       // dst = src1                         (8xy0)
    MoveFromVF, // dst = VF                           (8xF0)
    AddI,       // dst += imm         [dst usedef]    (7xkk)
    Add,        // dst += src1        [dst usedef]    (8xy4)
    Sub,        // dst -= src1        [dst usedef]    (8xy5)
    SubRev,     // dst = src1 - dst   [dst usedef]    (8xy7)
    And,        // dst &= src1        [dst usedef]    (8xy2)
    Or,         // dst |= src1        [dst usedef]    (8xy1)
    Xor,        // dst ^= src1        [dst usedef]    (8xy3)
    Shl,        // dst <<= 1          [dst usedef]    (8xxE)
    Shr,        // dst >>= 1          [dst usedef]    (8xx6)
    Rand,       // dst = rand & imm                   (Cxkk)

    // Control flow (labels are function-local ids)
    Label,      // imm = label id
    Jump,       // imm = label id
    BrEqI,      // if src1 == imm2 jump imm           (SNE+JP)
    BrNeI,      // if src1 != imm2 jump imm           (SE+JP)
    BrEq,       // if src1 == src2 jump imm           (9xy0+JP)
    BrNe,       // if src1 != src2 jump imm           (5xy0+JP)
    BrVF,       // if VF == imm2 jump imm             (SNE VF+JP); must directly
                //   follow the flag-setting op

    // Functions
    EnterFunc,  // defines the first imm vregs (parameters, vregs 0..imm-1)
    Call,       // dst? = name(args)                  (save/args/2NNN/restore)
    Ret,        // return src1?                       (move to VE + 00EE)

    // Memory
    LoadG,      // dst = mem[imm]                     (A+F065, via V0)
    StoreG,     // mem[imm] = src1                    (A+move+F055, via V0)
    LoadIdx,    // dst = mem[imm + src1]              (A+F01E+F065, via V0)
    StoreIdx,   // mem[imm + src2] = src1             (A+F01E+move+F055, via V0)

    // CHIP-8 specials
    Cls,        //                                    (00E0)
    Draw,       // sprite at (src1,src2), height imm2, data addr imm (DXYN)
    DrawNum,    // BCD digits of src1 at (src2,src3)  (F033 + font draws)
    KeyVal,     // dst = key src1 pressed ? 1 : 0     (SKP-based)
    WaitKey,    // dst = blocking key read            (F00A)
    Timer,      // dst = delay timer                  (F007)
    WaitTicks,  // block for src1 timer ticks         (F015 + poll loop)
    Beep,       // sound timer = src1                 (F018)

    Raw,        // inline asm: raw_pool[imm] opcodes emitted verbatim
};

struct IRInst {
    IROp op;
    VReg dst = NO_VREG;
    VReg src1 = NO_VREG;
    VReg src2 = NO_VREG;
    VReg src3 = NO_VREG;
    int imm = 0;    // immediate / label id / address / raw-pool index
    int imm2 = 0;   // secondary immediate (branch compare value, draw height)
    std::string name;             // callee name for Call
    std::vector<VReg> args;       // call arguments
    int line = 0, col = 0;        // source location for the source map

    // Operand roles (usedef operands appear in both lists)
    void collect_uses(std::vector<VReg>& out) const;
    void collect_defs(std::vector<VReg>& out) const;
};

struct IRFunction {
    std::string name;
    int param_count = 0;
    bool returns_value = false;
    std::vector<IRInst> insts;
    std::vector<std::vector<uint16_t>> raw_pool; // inline-asm opcode blobs
    VReg next_vreg = 0;
    int next_label = 0;

    VReg new_vreg() { return next_vreg++; }
    int new_label() { return next_label++; }
    IRInst& emit(IROp op) {
        insts.push_back(IRInst{op});
        return insts.back();
    }
};

// Result of register allocation for one function
struct Allocation {
    std::map<VReg, uint8_t> assignment;       // vreg -> physical register
    std::map<size_t, uint8_t> call_save_max;  // Call inst index -> highest live phys
    uint8_t max_phys = 0;                     // highest physical register used
    int spill_count = 0;                      // vregs spilled to memory
    uint16_t spill_end = 0;                   // next free address after spill slots
};

// Linear-scan register allocator with spill-everywhere rewriting.
// May insert LoadG/StoreG spill instructions into the function.
// spill_base: first free memory address for spill slots (grows upward).
class IRAllocator {
public:
    // Returns false if allocation failed (cannot happen in practice: spill
    // rounds always converge because rewritten uses have single-inst ranges)
    bool run(IRFunction& fn, uint16_t spill_base, Allocation& out);

private:
    struct Interval {
        VReg vreg;
        int start;
        int end;
    };
    void compute_intervals(const IRFunction& fn, std::vector<Interval>& out);
};

// Lowers allocated IR to bytecode. The emitter appends to `output`, records
// function-call fixups (label = callee name) and per-instruction source
// mappings, and returns the map of label id -> byte offset.
class IREmitter {
public:
    struct CallFixup {
        uint16_t address;
        std::string name;
    };

    // source_map_out: pairs of (byte offset, line/col) recorded per statement
    struct Mapping {
        uint16_t offset;
        int line, col;
    };

    void emit(const IRFunction& fn, const Allocation& alloc,
              std::vector<uint8_t>& output,
              std::vector<CallFixup>& call_fixups,
              std::vector<Mapping>& mappings,
              std::string& error);

private:
    void byte(std::vector<uint8_t>& out, uint8_t b) { out.push_back(b); }
    void op(std::vector<uint8_t>& out, uint16_t opcode) {
        out.push_back(static_cast<uint8_t>(opcode >> 8));
        out.push_back(static_cast<uint8_t>(opcode & 0xFF));
    }
    // Emit parallel register moves (src[i] -> dst[i]) using V0 as the
    // cycle-breaking scratch
    void parallel_moves(std::vector<uint8_t>& out,
                        std::vector<std::pair<uint8_t, uint8_t>> moves);
};

}
}
