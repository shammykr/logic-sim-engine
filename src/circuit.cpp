#include "logicsim/circuit.hpp"

#include <algorithm>
#include <queue>

namespace logicsim {

NetId Circuit::add_net(const std::string& name) {
    auto it = net_index_.find(name);
    if (it != net_index_.end()) return it->second;
    auto id = static_cast<NetId>(nets_.size());
    nets_.push_back(Net{name});
    net_index_.emplace(name, id);
    return id;
}

NetId Circuit::find_net(const std::string& name) const {
    auto it = net_index_.find(name);
    return it == net_index_.end() ? kNoNet : it->second;
}

NetId Circuit::constant_net(Logic value) {
    auto slot = static_cast<std::size_t>(value);
    if (constants_[slot] == kNoNet) {
        constants_[slot] = add_net(std::string("1'b") + to_char(value));
        nets_[constants_[slot]].is_constant = true;
        nets_[constants_[slot]].constant_value = value;
    }
    return constants_[slot];
}

GateId Circuit::add_gate(GateType type, std::string name, NetId output, const std::vector<NetId>& inputs,
                         std::uint32_t delay) {
    auto id = static_cast<GateId>(gates_.size());
    Net& out = nets_.at(output);
    if (out.is_constant) throw NetlistError("gate " + name + " drives a constant");
    if (out.is_input) throw NetlistError("gate " + name + " drives primary input " + out.name);
    if (out.driver != kNoGate) {
        throw NetlistError("net " + out.name + " has two drivers: " + gates_[out.driver].name + " and " + name);
    }
    out.driver = id;
    auto begin = static_cast<std::uint32_t>(inputs_.size());
    inputs_.insert(inputs_.end(), inputs.begin(), inputs.end());
    gates_.push_back(Gate{type, delay, output, begin, static_cast<std::uint32_t>(inputs_.size()), std::move(name)});
    return id;
}

void Circuit::mark_input(NetId n) {
    Net& net = nets_.at(n);
    if (net.driver != kNoGate) throw NetlistError("primary input " + net.name + " is driven by a gate");
    if (!net.is_input) primary_inputs_.push_back(n);
    net.is_input = true;
}

void Circuit::mark_output(NetId n) {
    if (!nets_.at(n).is_output) primary_outputs_.push_back(n);
    nets_[n].is_output = true;
}

void Circuit::finalize() {
    if (finalized_) return;
    const auto num_nets = nets_.size();

    // Fanout lists (CSR). A gate appears once per distinct input net.
    std::vector<std::uint32_t> degree(num_nets + 1, 0);
    std::vector<std::pair<NetId, GateId>> edges;
    edges.reserve(inputs_.size());
    for (GateId g = 0; g < gates_.size(); ++g) {
        const Gate& gate = gates_[g];
        for (std::uint32_t i = gate.in_begin; i < gate.in_end; ++i) {
            if (gate.type == GateType::Dff && i - gate.in_begin == kDffD) continue;  // D is not a trigger
            edges.emplace_back(inputs_[i], g);
        }
    }
    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    for (const auto& e : edges) ++degree[e.first + 1];
    fanout_offset_.assign(num_nets + 1, 0);
    for (std::size_t n = 0; n < num_nets; ++n) fanout_offset_[n + 1] = fanout_offset_[n] + degree[n + 1];
    fanout_.resize(edges.size());
    for (std::size_t i = 0; i < edges.size(); ++i) fanout_[i] = edges[i].second;  // edges are sorted by net

    // Levelize the combinational logic with Kahn's algorithm. Sources are primary inputs,
    // constants, undriven nets and flip-flop outputs; a combinational gate becomes ready once all
    // of its input nets are settled. Gates left over at the end sit on a combinational cycle.
    std::vector<std::uint32_t> pending(gates_.size(), 0);
    std::vector<std::uint32_t> level(num_nets, 0);
    std::queue<GateId> ready;
    for (GateId g = 0; g < gates_.size(); ++g) {
        const Gate& gate = gates_[g];
        if (gate.type == GateType::Dff) continue;
        for (std::uint32_t i = gate.in_begin; i < gate.in_end; ++i) {
            GateId d = nets_[inputs_[i]].driver;
            if (d != kNoGate && gates_[d].type != GateType::Dff) ++pending[g];
        }
        if (pending[g] == 0) ready.push(g);
    }
    topo_order_.clear();
    std::size_t combinational = gates_.size() - count(GateType::Dff);
    while (!ready.empty()) {
        GateId g = ready.front();
        ready.pop();
        topo_order_.push_back(g);
        const Gate& gate = gates_[g];
        std::uint32_t lvl = 0;
        for (std::uint32_t i = gate.in_begin; i < gate.in_end; ++i) lvl = std::max(lvl, level[inputs_[i]]);
        level[gate.output] = lvl + 1;
        logic_depth_ = std::max(logic_depth_, lvl + 1);
        // Count each consumer once per input pin that reads this net.
        for (const GateId* f = fanout_begin(gate.output); f != fanout_end(gate.output); ++f) {
            const Gate& consumer = gates_[*f];
            if (consumer.type == GateType::Dff) continue;
            std::uint32_t uses = 0;
            for (std::uint32_t i = consumer.in_begin; i < consumer.in_end; ++i) uses += inputs_[i] == gate.output;
            pending[*f] -= uses;
            if (pending[*f] == 0) ready.push(*f);
        }
    }
    if (topo_order_.size() != combinational) {
        // Every unprocessed gate has an unprocessed combinational driver, so walking backwards
        // from any of them must eventually revisit a gate: that gate is on the cycle.
        GateId g = 0;
        while (gates_[g].type == GateType::Dff || pending[g] == 0) ++g;
        std::vector<bool> seen(gates_.size(), false);
        while (!seen[g]) {
            seen[g] = true;
            const Gate& gate = gates_[g];
            for (std::uint32_t i = gate.in_begin; i < gate.in_end; ++i) {
                GateId d = nets_[inputs_[i]].driver;
                if (d != kNoGate && gates_[d].type != GateType::Dff && pending[d] != 0) {
                    g = d;
                    break;
                }
            }
        }
        throw NetlistError("combinational loop through gate " + gates_[g].name + " (net " +
                           nets_[gates_[g].output].name + "); loops must go through a flip-flop");
    }
    finalized_ = true;
}

std::size_t Circuit::count(GateType t) const {
    return static_cast<std::size_t>(
        std::count_if(gates_.begin(), gates_.end(), [t](const Gate& g) { return g.type == t; }));
}

std::size_t Circuit::memory_bytes() const {
    std::size_t bytes = nets_.capacity() * sizeof(Net) + gates_.capacity() * sizeof(Gate) +
                        inputs_.capacity() * sizeof(NetId) + fanout_.capacity() * sizeof(GateId) +
                        fanout_offset_.capacity() * sizeof(std::uint32_t) + topo_order_.capacity() * sizeof(GateId);
    for (const auto& n : nets_) bytes += n.name.capacity();
    for (const auto& g : gates_) bytes += g.name.capacity();
    return bytes;
}

}  // namespace logicsim
