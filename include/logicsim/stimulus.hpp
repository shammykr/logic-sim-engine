// Input stimulus: a stream of (time, input assignments) batches in increasing time order.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "logicsim/circuit.hpp"

namespace logicsim {

struct StimulusBatch {
    std::uint64_t time = 0;
    std::vector<std::pair<NetId, Logic>> assignments;
};

class Stimulus {
public:
    virtual ~Stimulus() = default;
    virtual bool next(StimulusBatch& batch) = 0;  // false when exhausted
    virtual std::uint64_t end_time() const = 0;   // simulate up to this time
};

// Stimulus file (.stim):
//   # comment
//   clock clk 10        clk = 0 at time 0, rises at 5, 15, 25, ... (period 10)
//   end 200             simulate until time 200 (default: last assignment)
//   0   rst=1 a=0 b[2]=1
//   12  rst=0
std::unique_ptr<Stimulus> load_stimulus_file(const std::string& path, const Circuit& circuit);

// Random vectors: `cycles` periods of length `period`. At the start of every period each primary
// input except the clock and reset gets a fresh random 0/1. The optional clock is 0 for the first
// half period and rises at period/2. The optional reset is 1 during the first period, then 0.
struct RandomStimulusConfig {
    std::uint64_t cycles = 1000;
    std::uint64_t period = 10;
    NetId clock = kNoNet;
    NetId reset = kNoNet;
    std::uint64_t seed = 1;
};
std::unique_ptr<Stimulus> make_random_stimulus(const Circuit& circuit, const RandomStimulusConfig& config);

}  // namespace logicsim
