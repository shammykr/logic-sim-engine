// Circuit graph: nets connected by gates.
//
// Nets are the graph's signals and gates its nodes. After all gates are added, finalize()
// validates the netlist (one driver per net), builds compact fanout lists (CSR layout: one
// contiguous array plus offsets) and levelizes the combinational logic. The combinational part
// must be a directed acyclic graph: flip-flops are the only allowed way to close a loop.
#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "logicsim/logic.hpp"

namespace logicsim {

using NetId = std::uint32_t;
using GateId = std::uint32_t;
inline constexpr NetId kNoNet = std::numeric_limits<NetId>::max();
inline constexpr GateId kNoGate = std::numeric_limits<GateId>::max();

struct Net {
    std::string name;
    GateId driver = kNoGate;  // gate driving this net, if any
    bool is_input = false;    // primary input (driven by the stimulus)
    bool is_output = false;   // primary output
    bool is_constant = false; // tie-off 1'b0 / 1'b1 / 1'bx
    Logic constant_value = Logic::X;
};

// Flip-flop pin order in Gate inputs: D, CLK and an optional active-high asynchronous reset.
inline constexpr std::uint32_t kDffD = 0;
inline constexpr std::uint32_t kDffClk = 1;
inline constexpr std::uint32_t kDffRst = 2;

struct Gate {
    GateType type;
    std::uint32_t delay;       // propagation delay in time units; 0 = next delta cycle
    NetId output;
    std::uint32_t in_begin;    // inputs are inputs_[in_begin, in_end)
    std::uint32_t in_end;
    std::string name;
    std::uint32_t num_inputs() const { return in_end - in_begin; }
};

class NetlistError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Circuit {
public:
    std::string module_name;

    NetId add_net(const std::string& name);              // returns the existing id if declared
    NetId find_net(const std::string& name) const;       // kNoNet if absent
    NetId constant_net(Logic value);                     // shared tie-off net
    GateId add_gate(GateType type, std::string name, NetId output, const std::vector<NetId>& inputs,
                    std::uint32_t delay);

    void mark_input(NetId n);
    void mark_output(NetId n);

    // Validates the netlist and builds fanout lists and levels. Throws NetlistError.
    void finalize();

    const std::vector<Net>& nets() const { return nets_; }
    const std::vector<Gate>& gates() const { return gates_; }
    const NetId* gate_inputs(const Gate& g) const { return inputs_.data() + g.in_begin; }

    // Gates that must be (re)considered when `n` changes. For a flip-flop this is only its CLK
    // and reset pins; a change on D alone does nothing until the next clock edge.
    const GateId* fanout_begin(NetId n) const { return fanout_.data() + fanout_offset_[n]; }
    const GateId* fanout_end(NetId n) const { return fanout_.data() + fanout_offset_[n + 1]; }

    const std::vector<NetId>& primary_inputs() const { return primary_inputs_; }
    const std::vector<NetId>& primary_outputs() const { return primary_outputs_; }
    const std::vector<GateId>& topological_order() const { return topo_order_; }  // combinational
    std::uint32_t logic_depth() const { return logic_depth_; }
    std::size_t count(GateType t) const;
    std::size_t memory_bytes() const;  // approximate footprint of the graph structures

private:
    std::vector<Net> nets_;
    std::vector<Gate> gates_;
    std::vector<NetId> inputs_;
    std::unordered_map<std::string, NetId> net_index_;
    std::vector<NetId> primary_inputs_;
    std::vector<NetId> primary_outputs_;
    NetId constants_[3] = {kNoNet, kNoNet, kNoNet};

    std::vector<std::uint32_t> fanout_offset_;
    std::vector<GateId> fanout_;
    std::vector<GateId> topo_order_;
    std::uint32_t logic_depth_ = 0;
    bool finalized_ = false;
};

}  // namespace logicsim
