/*
 * SCL - CHIP-8 Emulator, Debugger, and Compiler
 * Copyright (c) 2026 Sean Cornell <secornell@ucsd.edu>
 * MIT License
 *
 * IR register allocation (linear scan with spilling) and bytecode emission.
 */

#include <chip8/compiler/ir.h>
#include <algorithm>
#include <cassert>

namespace chip8 {
namespace compiler {

// ---------------------------------------------------------------------------
// Operand roles
// ---------------------------------------------------------------------------

void IRInst::collect_uses(std::vector<VReg>& out) const {
    auto add = [&](VReg v) { if (v != NO_VREG) out.push_back(v); };
    switch (op) {
        case IROp::Move:      add(src1); break;
        case IROp::AddI:      add(dst); break;                 // usedef
        case IROp::Add: case IROp::Sub: case IROp::SubRev:
        case IROp::And: case IROp::Or: case IROp::Xor:
            add(dst); add(src1); break;                        // dst usedef
        case IROp::Shl: case IROp::Shr:
            add(dst); break;                                   // usedef
        case IROp::BrEqI: case IROp::BrNeI:
            add(src1); break;
        case IROp::BrEq: case IROp::BrNe:
            add(src1); add(src2); break;
        case IROp::Call:
            for (VReg a : args) add(a);
            break;
        case IROp::Ret:       add(src1); break;
        case IROp::StoreG:    add(src1); break;
        case IROp::LoadIdx:   add(src1); break;
        case IROp::StoreIdx:  add(src1); add(src2); break;
        case IROp::Draw:      add(src1); add(src2); break;
        case IROp::DrawNum:   add(src1); add(src2); add(src3); break;
        case IROp::KeyVal:    add(src1); break;
        case IROp::WaitTicks: add(src1); break;
        case IROp::Beep:      add(src1); break;
        default: break;
    }
}

void IRInst::collect_defs(std::vector<VReg>& out) const {
    auto add = [&](VReg v) { if (v != NO_VREG) out.push_back(v); };
    switch (op) {
        case IROp::Const: case IROp::Move: case IROp::MoveFromVF:
        case IROp::Rand: case IROp::LoadG: case IROp::LoadIdx:
        case IROp::KeyVal: case IROp::WaitKey: case IROp::Timer:
            add(dst); break;
        case IROp::AddI:
        case IROp::Add: case IROp::Sub: case IROp::SubRev:
        case IROp::And: case IROp::Or: case IROp::Xor:
        case IROp::Shl: case IROp::Shr:
            add(dst); break;                                   // usedef
        case IROp::Call:
            add(dst); break;
        case IROp::EnterFunc:
            // Defines the parameter vregs 0..param_count-1; the builder
            // guarantees these ids. imm holds the parameter count.
            for (int i = 0; i < imm; i++) out.push_back(static_cast<VReg>(i));
            break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
// Live intervals
// ---------------------------------------------------------------------------

void IRAllocator::compute_intervals(const IRFunction& fn, std::vector<Interval>& out) {
    std::map<VReg, Interval> ivals;
    std::vector<VReg> tmp;

    // Occurrence list per vreg: (index, is_read). Needed to decide whether a
    // value is loop-carried (read before written inside a loop).
    std::map<VReg, std::vector<std::pair<int, bool>>> occurrences;

    // Base intervals: [first occurrence, last occurrence] in linear order
    for (size_t i = 0; i < fn.insts.size(); i++) {
        tmp.clear();
        fn.insts[i].collect_uses(tmp);
        for (VReg v : tmp) occurrences[v].push_back({static_cast<int>(i), true});
        size_t use_count = tmp.size();
        fn.insts[i].collect_defs(tmp);
        for (size_t k = use_count; k < tmp.size(); k++)
            occurrences[tmp[k]].push_back({static_cast<int>(i), false});
        for (VReg v : tmp) {
            auto it = ivals.find(v);
            if (it == ivals.end()) {
                ivals[v] = {v, static_cast<int>(i), static_cast<int>(i)};
            } else {
                it->second.end = static_cast<int>(i);
            }
        }
    }

    // Loop extension: a value live anywhere inside a loop must stay live for
    // the whole loop, or the back edge would see a stale register. For every
    // backward branch [target..branch], extend intersecting intervals to
    // cover the full range. Iterate to a fixpoint for nested loops.
    // Label positions:
    std::map<int, int> label_pos;
    for (size_t i = 0; i < fn.insts.size(); i++) {
        if (fn.insts[i].op == IROp::Label) label_pos[fn.insts[i].imm] = static_cast<int>(i);
    }
    struct BackEdge { int from, to; };
    std::vector<BackEdge> back_edges;
    for (size_t i = 0; i < fn.insts.size(); i++) {
        const IRInst& in = fn.insts[i];
        bool is_branch = in.op == IROp::Jump || in.op == IROp::BrEqI || in.op == IROp::BrNeI ||
                         in.op == IROp::BrEq || in.op == IROp::BrNe || in.op == IROp::BrVF;
        if (!is_branch) continue;
        auto it = label_pos.find(in.imm);
        if (it != label_pos.end() && it->second <= static_cast<int>(i)) {
            back_edges.push_back({static_cast<int>(i), it->second});
        }
    }

    // Sorted label positions for the dominance check below
    std::vector<int> label_positions;
    for (size_t i = 0; i < fn.insts.size(); i++) {
        if (fn.insts[i].op == IROp::Label) label_positions.push_back(static_cast<int>(i));
    }

    // A vreg needs whole-loop extension when its value can survive the back
    // edge: it starts before the loop, its first in-loop occurrence is a
    // read (loop-carried), or its first in-loop write does NOT dominate the
    // later occurrences. Dominance is approximated linearly: if any label
    // sits between the write and the last in-loop occurrence, control can
    // enter past the write (e.g. a conditional init like
    // 'if (restart) { head = 2; }' inside the game loop), so the value may
    // flow from the previous iteration and must stay live for the whole
    // loop. Spill reload/store pairs are adjacent with no label between
    // them, so they are never extended - which keeps spill rounds
    // convergent.
    auto needs_extension = [&](const Interval& iv, int to, int from) {
        if (iv.start < to) return true;
        auto oit = occurrences.find(iv.vreg);
        if (oit == occurrences.end()) return false;
        int first_idx = -1;
        bool first_is_read = false;
        int last_idx = -1;
        for (const auto& [idx, is_read] : oit->second) {
            if (idx < to || idx > from) continue;
            if (first_idx < 0) { first_idx = idx; first_is_read = is_read; }
            last_idx = idx;
        }
        if (first_idx < 0) return false;
        if (first_is_read) return true;
        // Any label strictly between the first write and the last occurrence
        // means the write may be bypassed while the uses still execute
        auto lo = std::upper_bound(label_positions.begin(), label_positions.end(), first_idx);
        return lo != label_positions.end() && *lo <= last_idx;
    };

    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& e : back_edges) {
            for (auto& [v, iv] : ivals) {
                // Interval intersects [to, from]?
                if (iv.start <= e.from && iv.end >= e.to) {
                    if (!needs_extension(iv, e.to, e.from)) continue;
                    if (iv.start > e.to)   { iv.start = e.to;   changed = true; }
                    if (iv.end   < e.from) { iv.end   = e.from; changed = true; }
                }
            }
        }
    }

    out.clear();
    out.reserve(ivals.size());
    for (auto& [v, iv] : ivals) out.push_back(iv);
    std::sort(out.begin(), out.end(), [](const Interval& a, const Interval& b) {
        if (a.start != b.start) return a.start < b.start;
        return a.vreg < b.vreg;
    });
}

// ---------------------------------------------------------------------------
// Linear scan with spill-everywhere rewriting
// ---------------------------------------------------------------------------

bool IRAllocator::run(IRFunction& fn, uint16_t spill_base, Allocation& out) {
    // Physical registers available for allocation: V1..VC and VE.
    static const uint8_t POOL[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0xA, 0xB, 0xC, 0xE};
    constexpr int POOL_SIZE = 13;

    std::map<VReg, uint16_t> spill_slots; // spilled vreg -> memory address
    uint16_t next_slot = spill_base;

    for (int round = 0; round < 32; round++) {
        std::vector<Interval> intervals;
        compute_intervals(fn, intervals);

        std::map<VReg, uint8_t> assignment;
        std::vector<Interval> active; // sorted by end
        std::set<uint8_t> free_regs(POOL, POOL + POOL_SIZE);
        std::vector<VReg> to_spill;

        for (const Interval& cur : intervals) {
            // Expire finished intervals
            for (auto it = active.begin(); it != active.end();) {
                if (it->end < cur.start) {
                    free_regs.insert(assignment[it->vreg]);
                    it = active.erase(it);
                } else {
                    ++it;
                }
            }

            if (!free_regs.empty()) {
                uint8_t reg = *free_regs.begin();
                free_regs.erase(free_regs.begin());
                assignment[cur.vreg] = reg;
                active.push_back(cur);
                std::sort(active.begin(), active.end(),
                          [](const Interval& a, const Interval& b) { return a.end < b.end; });
            } else {
                // Spill the interval with the furthest end
                Interval* victim = nullptr;
                for (auto& a : active) {
                    if (!victim || a.end > victim->end) victim = &a;
                }
                if (victim && victim->end > cur.end) {
                    to_spill.push_back(victim->vreg);
                    uint8_t reg = assignment[victim->vreg];
                    assignment.erase(victim->vreg);
                    assignment[cur.vreg] = reg;
                    // Replace victim in active with cur
                    *victim = cur;
                    std::sort(active.begin(), active.end(),
                              [](const Interval& a, const Interval& b) { return a.end < b.end; });
                } else {
                    to_spill.push_back(cur.vreg);
                }
            }
        }

        if (to_spill.empty()) {
            // Success: fill in the result
            out.assignment = std::move(assignment);
            out.max_phys = 0;
            for (auto& [v, p] : out.assignment) {
                if (p != 0xE && p > out.max_phys) out.max_phys = p;
            }
            // VE counts as "highest" only for the save-range computation below
            // Compute per-call save ranges: highest phys live strictly across
            // each call instruction
            std::vector<Interval> final_intervals;
            compute_intervals(fn, final_intervals);
            std::map<VReg, Interval> by_vreg;
            for (auto& iv : final_intervals) by_vreg[iv.vreg] = iv;

            for (size_t i = 0; i < fn.insts.size(); i++) {
                if (fn.insts[i].op != IROp::Call) continue;
                uint8_t save_max = 0;
                for (auto& [v, p] : out.assignment) {
                    auto it = by_vreg.find(v);
                    if (it == by_vreg.end()) continue;
                    // Live strictly across: starts before the call, ends after
                    if (it->second.start < static_cast<int>(i) &&
                        it->second.end > static_cast<int>(i)) {
                        if (p > save_max) save_max = p;
                    }
                }
                out.call_save_max[i] = save_max;
            }
            out.spill_count = static_cast<int>(spill_slots.size());
            out.spill_end = next_slot;
            return true;
        }

        // Assign slots to newly spilled vregs
        for (VReg v : to_spill) {
            if (!spill_slots.count(v)) {
                spill_slots[v] = next_slot++;
            }
        }

        // Rewrite: every use of a spilled vreg loads into a fresh vreg first;
        // every def stores from a fresh vreg after. Fresh vregs have
        // single-instruction ranges, so the next round strictly improves.
        std::set<VReg> spilled(to_spill.begin(), to_spill.end());
        std::vector<IRInst> rewritten;
        rewritten.reserve(fn.insts.size() * 2);
        std::vector<VReg> uses, defs;

        for (const IRInst& inst : fn.insts) {
            uses.clear(); defs.clear();
            inst.collect_uses(uses);
            inst.collect_defs(defs);

            IRInst copy = inst;
            std::map<VReg, VReg> replacement;

            // Loads before the instruction for spilled uses
            for (VReg v : uses) {
                if (!spilled.count(v) || replacement.count(v)) continue;
                VReg fresh = fn.next_vreg++;
                replacement[v] = fresh;
                IRInst load{IROp::LoadG};
                load.dst = fresh;
                load.imm = spill_slots[v];
                load.line = inst.line; load.col = inst.col;
                rewritten.push_back(load);
            }
            // Defs (that are not also uses) get a fresh target too
            for (VReg v : defs) {
                if (!spilled.count(v) || replacement.count(v)) continue;
                replacement[v] = fn.next_vreg++;
            }

            auto rep = [&](VReg& v) {
                auto it = replacement.find(v);
                if (it != replacement.end()) v = it->second;
            };
            rep(copy.dst); rep(copy.src1); rep(copy.src2); rep(copy.src3);
            for (VReg& a : copy.args) rep(a);
            rewritten.push_back(copy);

            // Stores after the instruction for spilled defs
            for (VReg v : defs) {
                if (!spilled.count(v)) continue;
                IRInst store{IROp::StoreG};
                store.src1 = replacement[v];
                store.imm = spill_slots[v];
                store.line = inst.line; store.col = inst.col;
                rewritten.push_back(store);
            }
        }
        fn.insts = std::move(rewritten);
    }

    return false; // did not converge (should be unreachable)
}

// ---------------------------------------------------------------------------
// Emission
// ---------------------------------------------------------------------------

void IREmitter::parallel_moves(std::vector<uint8_t>& out,
                               std::vector<std::pair<uint8_t, uint8_t>> moves) {
    // Remove no-ops
    moves.erase(std::remove_if(moves.begin(), moves.end(),
                               [](auto& m) { return m.first == m.second; }),
                moves.end());

    while (!moves.empty()) {
        bool progressed = false;
        for (size_t i = 0; i < moves.size(); i++) {
            uint8_t src = moves[i].first, dst = moves[i].second;
            // Safe to emit if no other pending move still reads dst
            bool blocked = false;
            for (size_t j = 0; j < moves.size(); j++) {
                if (j != i && moves[j].first == dst) { blocked = true; break; }
            }
            if (!blocked) {
                op(out, 0x8000 | (dst << 8) | (src << 4)); // LD dst, src
                moves.erase(moves.begin() + i);
                progressed = true;
                break;
            }
        }
        if (!progressed) {
            // Cycle: rotate through V0
            uint8_t src = moves[0].first;
            op(out, 0x8000 | (0 << 8) | (src << 4)); // LD V0, src
            // Everything that read src now reads V0
            for (auto& m : moves) {
                if (m.first == src) m.first = 0;
            }
        }
    }
}

void IREmitter::emit(const IRFunction& fn, const Allocation& alloc,
                     std::vector<uint8_t>& output,
                     std::vector<CallFixup>& call_fixups,
                     std::vector<Mapping>& mappings,
                     std::string& error) {
    constexpr uint16_t CALLER_SAVE_BASE = 0xF50;

    auto phys = [&](VReg v) -> uint8_t {
        auto it = alloc.assignment.find(v);
        if (it == alloc.assignment.end()) {
            // A vreg with no assignment only happens for values that are
            // never used; park them in V0 (harmless scratch write)
            return 0;
        }
        return it->second;
    };

    std::map<int, uint16_t> label_offsets;                 // label id -> byte offset
    std::vector<std::pair<uint16_t, int>> label_patches;   // (jump offset, label id)

    auto emit_jump_to = [&](int label) {
        auto it = label_offsets.find(label);
        uint16_t at = static_cast<uint16_t>(output.size());
        if (it != label_offsets.end()) {
            op(output, 0x1000 | ((0x200 + it->second) & 0x0FFF));
        } else {
            op(output, 0x1000); // placeholder
            label_patches.push_back({at, label});
        }
    };

    int last_line = -1;
    for (size_t idx = 0; idx < fn.insts.size(); idx++) {
        const IRInst& in = fn.insts[idx];

        if (in.line > 0 && in.line != last_line) {
            mappings.push_back({static_cast<uint16_t>(output.size()), in.line, in.col});
            last_line = in.line;
        }

        switch (in.op) {
            case IROp::Const:
                op(output, 0x6000 | (phys(in.dst) << 8) | (in.imm & 0xFF));
                break;
            case IROp::Move: {
                uint8_t d = phys(in.dst), s = phys(in.src1);
                op(output, 0x8000 | (d << 8) | (s << 4)); // peephole strips self-moves
                break;
            }
            case IROp::MoveFromVF:
                op(output, 0x8000 | (phys(in.dst) << 8) | (0xF << 4));
                break;
            case IROp::AddI:
                op(output, 0x7000 | (phys(in.dst) << 8) | (in.imm & 0xFF));
                break;
            case IROp::Add:
                op(output, 0x8004 | (phys(in.dst) << 8) | (phys(in.src1) << 4));
                break;
            case IROp::Sub:
                op(output, 0x8005 | (phys(in.dst) << 8) | (phys(in.src1) << 4));
                break;
            case IROp::SubRev:
                op(output, 0x8007 | (phys(in.dst) << 8) | (phys(in.src1) << 4));
                break;
            case IROp::And:
                op(output, 0x8002 | (phys(in.dst) << 8) | (phys(in.src1) << 4));
                break;
            case IROp::Or:
                op(output, 0x8001 | (phys(in.dst) << 8) | (phys(in.src1) << 4));
                break;
            case IROp::Xor:
                op(output, 0x8003 | (phys(in.dst) << 8) | (phys(in.src1) << 4));
                break;
            case IROp::Shl:
                op(output, 0x800E | (phys(in.dst) << 8) | (phys(in.dst) << 4));
                break;
            case IROp::Shr:
                op(output, 0x8006 | (phys(in.dst) << 8) | (phys(in.dst) << 4));
                break;
            case IROp::Rand:
                op(output, 0xC000 | (phys(in.dst) << 8) | (in.imm & 0xFF));
                break;

            case IROp::Label:
                label_offsets[in.imm] = static_cast<uint16_t>(output.size());
                break;
            case IROp::Jump:
                emit_jump_to(in.imm);
                break;
            case IROp::BrEqI:
                // Taken when src1 == imm2: SNE skips the jump when !=
                op(output, 0x4000 | (phys(in.src1) << 8) | (in.imm2 & 0xFF));
                emit_jump_to(in.imm);
                break;
            case IROp::BrNeI:
                op(output, 0x3000 | (phys(in.src1) << 8) | (in.imm2 & 0xFF));
                emit_jump_to(in.imm);
                break;
            case IROp::BrEq:
                op(output, 0x9000 | (phys(in.src1) << 8) | (phys(in.src2) << 4));
                emit_jump_to(in.imm);
                break;
            case IROp::BrNe:
                op(output, 0x5000 | (phys(in.src1) << 8) | (phys(in.src2) << 4));
                emit_jump_to(in.imm);
                break;
            case IROp::BrVF:
                // Taken when VF == imm2
                op(output, 0x4000 | (0xF << 8) | (in.imm2 & 0xFF)); // SNE VF, want
                emit_jump_to(in.imm);
                break;

            case IROp::EnterFunc: {
                // Move incoming arguments (V1..Vn) into their allocated homes
                std::vector<std::pair<uint8_t, uint8_t>> moves;
                for (int i = 0; i < in.imm; i++) {
                    VReg pv = static_cast<VReg>(i);
                    if (!alloc.assignment.count(pv)) continue; // unused param
                    moves.push_back({static_cast<uint8_t>(i + 1), phys(pv)});
                }
                parallel_moves(output, moves);
                break;
            }

            case IROp::Call: {
                uint8_t save_max = 0;
                auto sm = alloc.call_save_max.find(idx);
                if (sm != alloc.call_save_max.end()) save_max = sm->second;

                // Push frame + save live registers
                if (save_max > 0) {
                    op(output, 0xA000 | CALLER_SAVE_BASE);      // LD I, base
                    op(output, 0xFD1E);                         // ADD I, VD
                    op(output, 0x7D11);                         // ADD VD, 17
                    op(output, 0xF055 | (save_max << 8));       // LD [I], Vmax
                } else {
                    op(output, 0x7D11);                         // ADD VD, 17
                }

                // Marshal arguments into V1..Vn
                std::vector<std::pair<uint8_t, uint8_t>> moves;
                for (size_t i = 0; i < in.args.size() && i < 12; i++) {
                    moves.push_back({phys(in.args[i]), static_cast<uint8_t>(i + 1)});
                }
                parallel_moves(output, moves);

                call_fixups.push_back({static_cast<uint16_t>(output.size()), in.name});
                op(output, 0x2000);                             // CALL placeholder
                op(output, 0x7DEF);                             // ADD VD, -17

                bool want_result = (in.dst != NO_VREG);
                if (want_result && save_max >= 0xE) {
                    // VE is live in the caller: stash the return value in the
                    // frame's spare slot across the restore
                    op(output, 0x80E0);                                 // LD V0, VE
                    op(output, 0xA000 | (CALLER_SAVE_BASE + 16));       // LD I, spare
                    op(output, 0xFD1E);                                 // ADD I, VD
                    op(output, 0xF055);                                 // LD [I], V0
                    op(output, 0xA000 | CALLER_SAVE_BASE);
                    op(output, 0xFD1E);
                    op(output, 0xF065 | (save_max << 8));               // restore V0..VE
                    op(output, 0x7DEF);                                 // VD was restored stale
                    op(output, 0xA000 | (CALLER_SAVE_BASE + 16));
                    op(output, 0xFD1E);
                    op(output, 0xF065);                                 // LD V0, [I]
                    if (phys(in.dst) != 0) {
                        op(output, 0x8000 | (phys(in.dst) << 8));       // LD dst, V0
                    }
                } else {
                    if (save_max > 0) {
                        op(output, 0xA000 | CALLER_SAVE_BASE);
                        op(output, 0xFD1E);
                        op(output, 0xF065 | (save_max << 8));           // restore
                        if (save_max >= 0xD) {
                            op(output, 0x7DEF);                         // VD restored stale
                        }
                    }
                    if (want_result && phys(in.dst) != 0xE) {
                        op(output, 0x8000 | (phys(in.dst) << 8) | (0xE << 4)); // LD dst, VE
                    }
                }
                break;
            }

            case IROp::Ret:
                if (in.src1 != NO_VREG && phys(in.src1) != 0xE) {
                    op(output, 0x8000 | (0xE << 8) | (phys(in.src1) << 4)); // LD VE, src
                }
                op(output, 0x00EE);
                break;

            case IROp::LoadG:
                op(output, 0xA000 | (in.imm & 0x0FFF));
                op(output, 0xF065);                                     // LD V0, [I]
                if (phys(in.dst) != 0) {
                    op(output, 0x8000 | (phys(in.dst) << 8));           // LD dst, V0
                }
                break;
            case IROp::StoreG:
                if (phys(in.src1) != 0) {
                    op(output, 0x8000 | (phys(in.src1) << 4));          // LD V0, src
                }
                op(output, 0xA000 | (in.imm & 0x0FFF));
                op(output, 0xF055);                                     // LD [I], V0
                break;
            case IROp::LoadIdx:
                op(output, 0xA000 | (in.imm & 0x0FFF));
                op(output, 0xF01E | (phys(in.src1) << 8));              // ADD I, idx
                op(output, 0xF065);                                     // LD V0, [I]
                if (phys(in.dst) != 0) {
                    op(output, 0x8000 | (phys(in.dst) << 8));
                }
                break;
            case IROp::StoreIdx:
                op(output, 0xA000 | (in.imm & 0x0FFF));
                op(output, 0xF01E | (phys(in.src2) << 8));              // ADD I, idx
                if (phys(in.src1) != 0) {
                    op(output, 0x8000 | (phys(in.src1) << 4));          // LD V0, val
                }
                op(output, 0xF055);
                break;

            case IROp::Cls:
                op(output, 0x00E0);
                break;
            case IROp::Draw:
                op(output, 0xA000 | (in.imm & 0x0FFF));                 // LD I, sprite
                op(output, 0xD000 | (phys(in.src1) << 8) | (phys(in.src2) << 4) | (in.imm2 & 0xF));
                break;
            case IROp::DrawNum: {
                uint8_t v = phys(in.src1), x = phys(in.src2), y = phys(in.src3);
                op(output, 0xAFD0);                     // LD I, BCD scratch
                op(output, 0xF033 | (v << 8));          // BCD
                op(output, 0xF065);                     // V0 = hundreds
                op(output, 0xF029);                     // I = font(V0)
                op(output, 0xD005 | (x << 8) | (y << 4));
                op(output, 0x7005 | (x << 8));          // x += 5
                op(output, 0xAFD1);
                op(output, 0xF065);
                op(output, 0xF029);
                op(output, 0xD005 | (x << 8) | (y << 4));
                op(output, 0x7005 | (x << 8));          // x += 5
                op(output, 0xAFD2);
                op(output, 0xF065);
                op(output, 0xF029);
                op(output, 0xD005 | (x << 8) | (y << 4));
                op(output, 0x70F6 | (x << 8));          // x -= 10 (restore)
                break;
            }
            case IROp::KeyVal: {
                uint8_t d = phys(in.dst), k = phys(in.src1);
                if (d == k) {
                    // dst would clobber the key number before the test
                    op(output, 0x6001);                 // LD V0, 1
                    op(output, 0xE09E | (k << 8));      // SKP key
                    op(output, 0x6000);                 // LD V0, 0
                    op(output, 0x8000 | (d << 8));      // LD dst, V0
                } else {
                    op(output, 0x6001 | (d << 8));      // LD dst, 1
                    op(output, 0xE09E | (k << 8));      // SKP key
                    op(output, 0x6000 | (d << 8));      // LD dst, 0
                }
                break;
            }
            case IROp::WaitKey:
                op(output, 0xF00A | (phys(in.dst) << 8));
                break;
            case IROp::Timer:
                op(output, 0xF007 | (phys(in.dst) << 8));
                break;
            case IROp::WaitTicks: {
                op(output, 0xF015 | (phys(in.src1) << 8));  // LD DT, src
                uint16_t loop = static_cast<uint16_t>(output.size());
                op(output, 0xF007);                          // LD V0, DT
                op(output, 0x3000);                          // SE V0, 0
                op(output, 0x1000 | ((0x200 + loop) & 0x0FFF)); // JP loop
                break;
            }
            case IROp::Beep:
                op(output, 0xF018 | (phys(in.src1) << 8));
                break;

            case IROp::Raw:
                for (uint16_t opcode : fn.raw_pool[in.imm]) {
                    op(output, opcode);
                }
                break;
        }
    }

    // Resolve forward label references
    for (const auto& [at, label] : label_patches) {
        auto it = label_offsets.find(label);
        if (it == label_offsets.end()) {
            error = "internal: unresolved IR label in " + fn.name;
            return;
        }
        uint16_t opcode = 0x1000 | ((0x200 + it->second) & 0x0FFF);
        output[at] = static_cast<uint8_t>(opcode >> 8);
        output[at + 1] = static_cast<uint8_t>(opcode & 0xFF);
    }
}

}
}
