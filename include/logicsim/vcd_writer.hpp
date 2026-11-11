// Value Change Dump (IEEE 1364 section 18) writer.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "logicsim/simulator.hpp"

namespace logicsim {

// Writes every net of the circuit as a 1-bit wire. Changes are recorded once per time step,
// after the step has settled, so zero-width glitches between delta cycles are not dumped
// (the same convention as common Verilog simulators' $dumpvars output).
class VcdWriter : public ChangeObserver {
public:
    VcdWriter(const Circuit& circuit, const std::string& path, const std::string& timescale = "1ns");
    ~VcdWriter() override;
    VcdWriter(const VcdWriter&) = delete;
    VcdWriter& operator=(const VcdWriter&) = delete;

    void end_time_step(std::uint64_t time, const std::vector<NetId>& changed,
                       const std::vector<Logic>& values) override;
    void finish(std::uint64_t end_time);  // writes a final timestamp and flushes

    std::uint64_t changes_written() const { return changes_; }
    std::uint64_t bytes_written() const { return bytes_; }

private:
    void write_header(const std::string& timescale);
    void write_time(std::uint64_t time);
    void write_value(NetId net, Logic v);
    void flush();

    const Circuit& circuit_;
    std::FILE* file_;
    std::vector<std::string> ids_;
    std::vector<Logic> dumped_;  // last value written per net
    std::string buffer_;
    bool started_ = false;
    std::uint64_t last_time_ = 0;
    std::uint64_t changes_ = 0;
    std::uint64_t bytes_ = 0;
};

}  // namespace logicsim
