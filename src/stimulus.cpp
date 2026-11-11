#include "logicsim/stimulus.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace logicsim {
namespace {

class FileStimulus : public Stimulus {
public:
    FileStimulus(std::map<std::uint64_t, std::vector<std::pair<NetId, Logic>>> batches, std::uint64_t end)
        : batches_(std::move(batches)), it_(batches_.begin()), end_(end) {}

    bool next(StimulusBatch& batch) override {
        if (it_ == batches_.end()) return false;
        batch.time = it_->first;
        batch.assignments = it_->second;
        ++it_;
        return true;
    }
    std::uint64_t end_time() const override { return end_; }

private:
    std::map<std::uint64_t, std::vector<std::pair<NetId, Logic>>> batches_;
    std::map<std::uint64_t, std::vector<std::pair<NetId, Logic>>>::const_iterator it_;
    std::uint64_t end_;
};

class RandomStimulus : public Stimulus {
public:
    RandomStimulus(const Circuit& circuit, const RandomStimulusConfig& cfg) : cfg_(cfg), rng_(cfg.seed) {
        if (cfg.period < 2) throw std::runtime_error("period must be at least 2");
        for (NetId n : circuit.primary_inputs()) {
            if (n != cfg.clock && n != cfg.reset) data_inputs_.push_back(n);
        }
    }

    // Per period k: time k*P (clock low, reset, new data), then time k*P + P/2 (clock rises).
    bool next(StimulusBatch& batch) override {
        if (cycle_ >= cfg_.cycles) return false;
        batch.assignments.clear();
        if (!rising_half_) {
            batch.time = cycle_ * cfg_.period;
            if (cfg_.clock != kNoNet) batch.assignments.emplace_back(cfg_.clock, Logic::Zero);
            if (cfg_.reset != kNoNet && cycle_ <= 1) {
                batch.assignments.emplace_back(cfg_.reset, cycle_ == 0 ? Logic::One : Logic::Zero);
            }
            for (NetId n : data_inputs_) {
                batch.assignments.emplace_back(n, (rng_() & 1U) ? Logic::One : Logic::Zero);
            }
            if (cfg_.clock != kNoNet) {
                rising_half_ = true;
            } else {
                ++cycle_;
            }
        } else {
            batch.time = cycle_ * cfg_.period + cfg_.period / 2;
            batch.assignments.emplace_back(cfg_.clock, Logic::One);
            rising_half_ = false;
            ++cycle_;
        }
        return true;
    }
    std::uint64_t end_time() const override { return cfg_.cycles * cfg_.period; }

private:
    RandomStimulusConfig cfg_;
    std::mt19937_64 rng_;
    std::vector<NetId> data_inputs_;
    std::uint64_t cycle_ = 0;
    bool rising_half_ = false;
};

Logic parse_value(const std::string& v, const std::string& where) {
    if (v == "0") return Logic::Zero;
    if (v == "1") return Logic::One;
    if (v == "x" || v == "X") return Logic::X;
    throw std::runtime_error(where + ": value must be 0, 1 or x, got '" + v + "'");
}

}  // namespace

std::unique_ptr<Stimulus> load_stimulus_file(const std::string& path, const Circuit& circuit) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::map<std::uint64_t, std::vector<std::pair<NetId, Logic>>> batches;
    std::vector<std::pair<NetId, std::uint64_t>> clocks;
    std::uint64_t end = 0;
    bool explicit_end = false;
    std::string line;
    int lineno = 0;

    auto input_net = [&](const std::string& name, const std::string& where) {
        NetId n = circuit.find_net(name);
        if (n == kNoNet) throw std::runtime_error(where + ": unknown net '" + name + "'");
        if (!circuit.nets()[n].is_input) throw std::runtime_error(where + ": '" + name + "' is not a primary input");
        return n;
    };

    while (std::getline(in, line)) {
        ++lineno;
        std::string where = path + ":" + std::to_string(lineno);
        if (auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);
        std::istringstream ss(line);
        std::string first;
        if (!(ss >> first)) continue;
        if (first == "clock") {
            std::string name;
            std::uint64_t period = 0;
            if (!(ss >> name >> period) || period < 2) throw std::runtime_error(where + ": expected 'clock <net> <period>'");
            clocks.emplace_back(input_net(name, where), period);
        } else if (first == "end") {
            if (!(ss >> end)) throw std::runtime_error(where + ": expected 'end <time>'");
            explicit_end = true;
        } else {
            std::uint64_t time = 0;
            try {
                std::size_t used = 0;
                time = std::stoull(first, &used);
                if (used != first.size()) throw std::invalid_argument(first);
            } catch (const std::exception&) {
                throw std::runtime_error(where + ": expected a time, 'clock' or 'end', got '" + first + "'");
            }
            std::string assignment;
            while (ss >> assignment) {
                auto eq = assignment.find('=');
                if (eq == std::string::npos) throw std::runtime_error(where + ": expected net=value, got '" + assignment + "'");
                batches[time].emplace_back(input_net(assignment.substr(0, eq), where),
                                           parse_value(assignment.substr(eq + 1), where));
            }
            if (!explicit_end) end = std::max(end, time);
        }
    }
    for (const auto& [net, period] : clocks) {
        for (std::uint64_t t = 0; t <= end; t += period / 2) {
            batches[t].emplace_back(net, (t / (period / 2)) % 2 ? Logic::One : Logic::Zero);
        }
    }
    return std::make_unique<FileStimulus>(std::move(batches), end);
}

std::unique_ptr<Stimulus> make_random_stimulus(const Circuit& circuit, const RandomStimulusConfig& config) {
    return std::make_unique<RandomStimulus>(circuit, config);
}

}  // namespace logicsim
