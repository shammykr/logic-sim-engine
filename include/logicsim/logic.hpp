// Three-valued logic (0, 1, X) and gate evaluation.
#pragma once

#include <cstdint>

namespace logicsim {

enum class Logic : std::uint8_t { Zero = 0, One = 1, X = 2 };

inline char to_char(Logic v) {
    switch (v) {
        case Logic::Zero: return '0';
        case Logic::One: return '1';
        default: return 'x';
    }
}

inline Logic logic_not(Logic v) {
    if (v == Logic::X) return Logic::X;
    return v == Logic::Zero ? Logic::One : Logic::Zero;
}

enum class GateType : std::uint8_t { And, Or, Nand, Nor, Xor, Xnor, Not, Buf, Dff };

inline const char* gate_type_name(GateType t) {
    switch (t) {
        case GateType::And: return "and";
        case GateType::Or: return "or";
        case GateType::Nand: return "nand";
        case GateType::Nor: return "nor";
        case GateType::Xor: return "xor";
        case GateType::Xnor: return "xnor";
        case GateType::Not: return "not";
        case GateType::Buf: return "buf";
        case GateType::Dff: return "dff";
    }
    return "?";
}

// Evaluate a combinational gate over `n` input values.
//  and/nand: any 0 -> 0, else any X -> X, else 1 (then inverted for nand)
//  or/nor:   any 1 -> 1, else any X -> X, else 0 (then inverted for nor)
//  xor/xnor: any X -> X, else parity
inline Logic evaluate(GateType type, const Logic* in, std::uint32_t n) {
    switch (type) {
        case GateType::And:
        case GateType::Nand: {
            bool unknown = false;
            for (std::uint32_t i = 0; i < n; ++i) {
                if (in[i] == Logic::Zero) return type == GateType::And ? Logic::Zero : Logic::One;
                unknown |= in[i] == Logic::X;
            }
            if (unknown) return Logic::X;
            return type == GateType::And ? Logic::One : Logic::Zero;
        }
        case GateType::Or:
        case GateType::Nor: {
            bool unknown = false;
            for (std::uint32_t i = 0; i < n; ++i) {
                if (in[i] == Logic::One) return type == GateType::Or ? Logic::One : Logic::Zero;
                unknown |= in[i] == Logic::X;
            }
            if (unknown) return Logic::X;
            return type == GateType::Or ? Logic::Zero : Logic::One;
        }
        case GateType::Xor:
        case GateType::Xnor: {
            unsigned parity = 0;
            for (std::uint32_t i = 0; i < n; ++i) {
                if (in[i] == Logic::X) return Logic::X;
                parity ^= static_cast<unsigned>(in[i]);
            }
            if (type == GateType::Xnor) parity ^= 1U;
            return parity ? Logic::One : Logic::Zero;
        }
        case GateType::Not: return logic_not(in[0]);
        case GateType::Buf: return in[0];
        case GateType::Dff: break;  // sequential; handled by the simulator
    }
    return Logic::X;
}

}  // namespace logicsim
