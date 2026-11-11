// Event-driven simulation engine.
//
// Events (a net taking a new value at a time) wait in a min-heap ordered by (time, sequence).
// The engine pops every event of the earliest time and then runs delta cycles at that time:
//
//   delta N:   apply this delta's events to the nets; collect the nets that really changed
//              -> each combinational gate in their fanout is evaluated once (dirty flag)
//              -> flip-flops whose clock rose (or reset asserted) sample D
//              -> outputs with zero delay become delta N+1 events at the same time;
//                 outputs with a delay go back into the heap at time + delay
//   ...until a delta produces no events: the circuit is stable, simulation time may advance.
#pragma once

#include <cstdint>
#include <queue>
#include <stdexcept>
#include <vector>

#include "logicsim/circuit.hpp"

namespace logicsim {

struct Event {
    std::uint64_t time;
    std::uint64_t seq;  // FIFO order among events with the same time
    NetId net;
    Logic value;
};

struct SimOptions {
    std::uint32_t max_deltas = 10000;  // per time step; more means an oscillation
};

struct SimStats {
    std::uint64_t time_steps = 0;          // distinct simulation times processed
    std::uint64_t delta_cycles = 0;        // total delta cycles over all time steps
    std::uint32_t max_deltas_per_step = 0; // deepest delta chain in one time step
    std::uint64_t events_scheduled = 0;    // events created
    std::uint64_t events_processed = 0;    // events popped and applied
    std::uint64_t net_changes = 0;         // processed events that changed a net
    std::uint64_t gate_evaluations = 0;    // combinational gate evaluations
    std::uint64_t ff_updates = 0;          // flip-flop clock edges / resets acted on
};

// Receives the nets that changed during a time step, once the step has settled.
class ChangeObserver {
public:
    virtual ~ChangeObserver() = default;
    virtual void end_time_step(std::uint64_t time, const std::vector<NetId>& changed,
                               const std::vector<Logic>& values) = 0;
};

class SimulationError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Simulator {
public:
    Simulator(const Circuit& circuit, SimOptions options = {});
    ~Simulator();
    Simulator(const Simulator&) = delete;
    Simulator& operator=(const Simulator&) = delete;

    void set_observer(ChangeObserver* observer) { observer_ = observer; }

    // Drive a primary input to `value` at `time` (must not be in the past).
    void set_input(std::uint64_t time, NetId net, Logic value);

    // Process every event with time <= `until`.
    void run_until(std::uint64_t until);

    std::uint64_t now() const { return now_; }
    Logic value(NetId n) const { return values_[n]; }
    const std::vector<Logic>& values() const { return values_; }
    const SimStats& stats() const { return stats_; }

private:
    struct Later {
        bool operator()(const Event* a, const Event* b) const {
            return a->time != b->time ? a->time > b->time : a->seq > b->seq;
        }
    };

    void initialize();
    void process_time_step(std::uint64_t time);
    void schedule(NetId net, Logic value, std::uint32_t delay);
    void trigger_flip_flop(GateId g, NetId changed, Logic old_value);
    Event* new_event(std::uint64_t time, NetId net, Logic value);
    void free_event(Event* e);

    const Circuit& circuit_;
    SimOptions options_;
    ChangeObserver* observer_ = nullptr;

    std::vector<Logic> values_;
    std::priority_queue<Event*, std::vector<Event*>, Later> heap_;
    std::vector<Event*> current_delta_;
    std::vector<Event*> next_delta_;
    std::vector<std::pair<NetId, Logic>> changed_;  // (net, previous value) in this delta
    std::vector<GateId> to_evaluate_;
    std::vector<std::uint8_t> dirty_;               // gate already queued in this delta
    std::vector<Logic> scratch_;                    // gathered gate input values

    std::vector<NetId> step_changed_;               // nets changed in this time step (for observer)
    std::vector<std::uint8_t> in_step_changed_;

    std::uint64_t now_ = 0;
    std::uint64_t seq_ = 0;
    bool initialized_ = false;
    SimStats stats_;
};

}  // namespace logicsim
