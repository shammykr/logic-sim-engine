#include "logicsim/simulator.hpp"

#include <algorithm>
#include <string>

namespace logicsim {

Simulator::Simulator(const Circuit& circuit, SimOptions options)
    : circuit_(circuit),
      options_(options),
      values_(circuit.nets().size(), Logic::X),
      dirty_(circuit.gates().size(), 0),
      in_step_changed_(circuit.nets().size(), 0) {
    std::uint32_t max_fanin = 1;
    for (const auto& g : circuit.gates()) max_fanin = std::max(max_fanin, g.num_inputs());
    scratch_.resize(max_fanin);
    for (NetId n = 0; n < circuit.nets().size(); ++n) {
        if (circuit.nets()[n].is_constant) values_[n] = circuit.nets()[n].constant_value;
    }
}

Simulator::~Simulator() {
    while (!heap_.empty()) {
        free_event(heap_.top());
        heap_.pop();
    }
    for (Event* e : current_delta_) free_event(e);
    for (Event* e : next_delta_) free_event(e);
}

Event* Simulator::new_event(std::uint64_t time, NetId net, Logic value) {
    return new Event{time, seq_++, net, value};
}

void Simulator::free_event(Event* e) { delete e; }

// At time 0 every combinational gate is evaluated once, so gates fed only by constants (or
// flip-flops) produce a value without waiting for an input change.
void Simulator::initialize() {
    initialized_ = true;
    now_ = 0;
    for (GateId g : circuit_.topological_order()) {
        if (!dirty_[g]) {
            dirty_[g] = 1;
            to_evaluate_.push_back(g);
        }
    }
    // Evaluated in delta 0 of time step 0 (see process_time_step).
}

void Simulator::set_input(std::uint64_t time, NetId net, Logic value) {
    if (!circuit_.nets().at(net).is_input) {
        throw SimulationError("net " + circuit_.nets()[net].name + " is not a primary input");
    }
    if (initialized_ && time <= now_ && stats_.time_steps > 0) {
        throw SimulationError("input change at time " + std::to_string(time) + " is in the past");
    }
    heap_.push(new_event(time, net, value));
    ++stats_.events_scheduled;
}

void Simulator::run_until(std::uint64_t until) {
    if (!initialized_) {
        initialize();
        process_time_step(0);
    }
    while (!heap_.empty() && heap_.top()->time <= until) process_time_step(heap_.top()->time);
}

void Simulator::schedule(NetId net, Logic value, std::uint32_t delay) {
    Event* e = new_event(now_ + delay, net, value);
    ++stats_.events_scheduled;
    if (delay == 0) next_delta_.push_back(e);
    else heap_.push(e);
}

void Simulator::process_time_step(std::uint64_t time) {
    now_ = time;
    ++stats_.time_steps;
    while (!heap_.empty() && heap_.top()->time == time) {
        current_delta_.push_back(heap_.top());
        heap_.pop();
    }

    const auto& gates = circuit_.gates();
    std::uint32_t deltas = 0;
    // Gates queued by initialize() are evaluated in the first delta even if no event arrived.
    while (!current_delta_.empty() || !to_evaluate_.empty()) {
        if (++deltas > options_.max_deltas) {
            throw SimulationError("no convergence after " + std::to_string(options_.max_deltas) +
                                  " delta cycles at time " + std::to_string(time));
        }
        ++stats_.delta_cycles;

        // 1. Apply this delta's events.
        changed_.clear();
        for (Event* e : current_delta_) {
            ++stats_.events_processed;
            Logic old = values_[e->net];
            if (old != e->value) {
                values_[e->net] = e->value;
                changed_.emplace_back(e->net, old);
                ++stats_.net_changes;
                if (observer_ && !in_step_changed_[e->net]) {
                    in_step_changed_[e->net] = 1;
                    step_changed_.push_back(e->net);
                }
            }
            free_event(e);
        }
        current_delta_.clear();

        // 2. Find the gates affected by the changes.
        for (const auto& [net, old] : changed_) {
            for (const GateId* f = circuit_.fanout_begin(net); f != circuit_.fanout_end(net); ++f) {
                if (gates[*f].type == GateType::Dff) {
                    trigger_flip_flop(*f, net, old);
                } else if (!dirty_[*f]) {
                    dirty_[*f] = 1;
                    to_evaluate_.push_back(*f);
                }
            }
        }

        // 3. Evaluate each affected gate once; zero-delay outputs land in the next delta.
        for (GateId g : to_evaluate_) {
            dirty_[g] = 0;
            const Gate& gate = gates[g];
            const NetId* in = circuit_.gate_inputs(gate);
            const std::uint32_t n = gate.num_inputs();
            for (std::uint32_t i = 0; i < n; ++i) scratch_[i] = values_[in[i]];
            ++stats_.gate_evaluations;
            schedule(gate.output, evaluate(gate.type, scratch_.data(), n), gate.delay);
        }
        to_evaluate_.clear();

        std::swap(current_delta_, next_delta_);
    }
    stats_.max_deltas_per_step = std::max(stats_.max_deltas_per_step, deltas);

    if (observer_) {
        observer_->end_time_step(time, step_changed_, values_);
        for (NetId n : step_changed_) in_step_changed_[n] = 0;
        step_changed_.clear();
    }
}

// Positive-edge flip-flop with optional active-high asynchronous reset. Called when its CLK or
// RST net changed. D is sampled as it is in the current delta; Q changes in a later delta (or
// after the flip-flop's delay), so flip-flops clocked together never see each other's new Q.
void Simulator::trigger_flip_flop(GateId g, NetId changed, Logic old_value) {
    const Gate& ff = circuit_.gates()[g];
    const NetId* in = circuit_.gate_inputs(ff);
    if (ff.num_inputs() > kDffRst) {
        Logic rst = values_[in[kDffRst]];
        if (rst == Logic::One) {  // reset asserted (or held while the clock ticks)
            ++stats_.ff_updates;
            schedule(ff.output, Logic::Zero, ff.delay);
            return;
        }
        if (changed == in[kDffRst] && changed != in[kDffClk]) return;  // reset released: no edge
        if (rst == Logic::X && changed == in[kDffClk]) {
            ++stats_.ff_updates;
            schedule(ff.output, Logic::X, ff.delay);
            return;
        }
    }
    if (changed != in[kDffClk]) return;
    Logic clk = values_[in[kDffClk]];
    // Verilog posedge: 0->1, 0->x, x->1. An edge to/from x makes Q unknown.
    bool posedge = (old_value == Logic::Zero && clk != Logic::Zero) || (old_value == Logic::X && clk == Logic::One);
    if (!posedge) return;
    ++stats_.ff_updates;
    bool clean = old_value == Logic::Zero && clk == Logic::One;
    schedule(ff.output, clean ? values_[in[kDffD]] : Logic::X, ff.delay);
}

}  // namespace logicsim
