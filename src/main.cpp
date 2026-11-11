// logicsim: command-line driver. Parses a netlist, applies stimulus, simulates, writes VCD.
#include <sys/resource.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

#include "logicsim/simulator.hpp"
#include "logicsim/stimulus.hpp"
#include "logicsim/vcd_writer.hpp"
#include "logicsim/verilog_parser.hpp"

using namespace logicsim;

namespace {

void usage() {
    std::cerr <<
        "usage: logicsim <netlist.v> (--stim <file.stim> | --random <cycles>) [options]\n"
        "\n"
        "stimulus:\n"
        "  --stim FILE        stimulus file (time-stamped input assignments, clocks)\n"
        "  --random N         N cycles of random input vectors\n"
        "  --period P         cycle length in time units for --random (default 10)\n"
        "  --clock NET        clock input for --random: low for P/2, then high\n"
        "  --reset NET        reset input for --random: 1 during the first cycle, then 0\n"
        "  --seed S           random seed (default 1)\n"
        "\n"
        "output:\n"
        "  --vcd FILE         write a VCD waveform of every net\n"
        "  --outputs          print the final value of every primary output\n"
        "  --quiet            only print the statistics block\n"
        "\n"
        "engine:\n"
        "  --max-deltas N     delta cycles allowed per time step before giving up (default 10000)\n";
}

double peak_rss_mb() {
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
#ifdef __APPLE__
    return static_cast<double>(usage.ru_maxrss) / (1024.0 * 1024.0);  // bytes
#else
    return static_cast<double>(usage.ru_maxrss) / 1024.0;  // kilobytes
#endif
}

using Clock = std::chrono::steady_clock;
double seconds_since(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

}  // namespace

int main(int argc, char** argv) {
    std::string netlist, stim_file, vcd_file, clock_name, reset_name;
    std::uint64_t random_cycles = 0;
    RandomStimulusConfig random_cfg;
    SimOptions options;
    bool print_outputs = false, quiet = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << a << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--stim") stim_file = value();
        else if (a == "--random") random_cycles = std::stoull(value());
        else if (a == "--period") random_cfg.period = std::stoull(value());
        else if (a == "--clock") clock_name = value();
        else if (a == "--reset") reset_name = value();
        else if (a == "--seed") random_cfg.seed = std::stoull(value());
        else if (a == "--vcd") vcd_file = value();
        else if (a == "--outputs") print_outputs = true;
        else if (a == "--quiet") quiet = true;
        else if (a == "--max-deltas") options.max_deltas = static_cast<std::uint32_t>(std::stoul(value()));
        else if (a == "-h" || a == "--help") {
            usage();
            return 0;
        } else if (!a.empty() && a[0] == '-') {
            std::cerr << "unknown option " << a << "\n";
            usage();
            return 2;
        } else {
            netlist = a;
        }
    }
    if (netlist.empty() || (stim_file.empty() == (random_cycles == 0))) {
        usage();
        return 2;
    }

    try {
        auto t0 = Clock::now();
        Circuit circuit = parse_verilog_file(netlist);
        double parse_s = seconds_since(t0);

        std::unique_ptr<Stimulus> stimulus;
        if (!stim_file.empty()) {
            stimulus = load_stimulus_file(stim_file, circuit);
        } else {
            auto find_input = [&](const std::string& name) {
                if (name.empty()) return kNoNet;
                NetId n = circuit.find_net(name);
                if (n == kNoNet || !circuit.nets()[n].is_input) {
                    throw std::runtime_error("'" + name + "' is not a primary input");
                }
                return n;
            };
            random_cfg.cycles = random_cycles;
            random_cfg.clock = find_input(clock_name);
            random_cfg.reset = find_input(reset_name);
            stimulus = make_random_stimulus(circuit, random_cfg);
        }

        Simulator sim(circuit, options);
        std::unique_ptr<VcdWriter> vcd;
        if (!vcd_file.empty()) {
            vcd = std::make_unique<VcdWriter>(circuit, vcd_file);
            sim.set_observer(vcd.get());
        }

        auto t1 = Clock::now();
        StimulusBatch batch;
        std::uint64_t input_changes = 0;
        while (stimulus->next(batch)) {
            if (batch.time > 0) sim.run_until(batch.time - 1);
            for (const auto& [net, v] : batch.assignments) sim.set_input(batch.time, net, v);
            input_changes += batch.assignments.size();
        }
        sim.run_until(stimulus->end_time());
        if (vcd) vcd->finish(stimulus->end_time());
        double sim_s = seconds_since(t1);

        const SimStats& s = sim.stats();
        if (!quiet && print_outputs) {
            std::cout << "final outputs at t=" << stimulus->end_time() << ":\n";
            for (NetId n : circuit.primary_outputs()) {
                std::cout << "  " << circuit.nets()[n].name << " = " << to_char(sim.value(n)) << "\n";
            }
        }
        std::printf("netlist              %s (module %s)\n", netlist.c_str(), circuit.module_name.c_str());
        std::printf("gates                %zu (%zu flip-flops), nets %zu, inputs %zu, outputs %zu, logic depth %u\n",
                    circuit.gates().size(), circuit.count(GateType::Dff), circuit.nets().size(),
                    circuit.primary_inputs().size(), circuit.primary_outputs().size(), circuit.logic_depth());
        std::printf("parse_seconds        %.3f\n", parse_s);
        std::printf("sim_seconds          %.3f\n", sim_s);
        std::printf("sim_end_time         %llu\n", static_cast<unsigned long long>(stimulus->end_time()));
        std::printf("input_changes        %llu\n", static_cast<unsigned long long>(input_changes));
        std::printf("time_steps           %llu\n", static_cast<unsigned long long>(s.time_steps));
        std::printf("delta_cycles         %llu (max %u in one step)\n",
                    static_cast<unsigned long long>(s.delta_cycles), s.max_deltas_per_step);
        std::printf("events_scheduled     %llu\n", static_cast<unsigned long long>(s.events_scheduled));
        std::printf("events_processed     %llu\n", static_cast<unsigned long long>(s.events_processed));
        std::printf("net_changes          %llu\n", static_cast<unsigned long long>(s.net_changes));
        std::printf("gate_evaluations     %llu\n", static_cast<unsigned long long>(s.gate_evaluations));
        std::printf("ff_updates           %llu\n", static_cast<unsigned long long>(s.ff_updates));
        std::printf("events_per_second    %.0f\n", sim_s > 0 ? static_cast<double>(s.events_processed) / sim_s : 0.0);
        if (vcd) {
            std::printf("vcd                  %s (%llu changes, %.1f MB)\n", vcd_file.c_str(),
                        static_cast<unsigned long long>(vcd->changes_written()),
                        static_cast<double>(vcd->bytes_written()) / 1e6);
        }
        std::printf("peak_rss_mb          %.1f\n", peak_rss_mb());
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
