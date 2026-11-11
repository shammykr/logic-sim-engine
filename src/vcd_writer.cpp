#include "logicsim/vcd_writer.hpp"

#include <ctime>
#include <stdexcept>

namespace logicsim {
namespace {

// Short identifier codes from the printable ASCII range '!'..'~' (base 94).
std::string vcd_id(std::size_t n) {
    std::string id;
    do {
        id.push_back(static_cast<char>('!' + n % 94));
        n /= 94;
    } while (n > 0);
    return id;
}

// VCD references can't contain spaces; a bit select becomes "name [i]" which viewers group as a
// vector element.
std::string vcd_reference(const std::string& name) {
    auto bracket = name.find('[');
    std::string base = bracket == std::string::npos ? name : name.substr(0, bracket);
    for (auto& c : base) {
        if (c == ' ' || c == '\t') c = '_';
    }
    return bracket == std::string::npos ? base : base + " " + name.substr(bracket);
}

constexpr std::size_t kFlushBytes = 1 << 20;

}  // namespace

VcdWriter::VcdWriter(const Circuit& circuit, const std::string& path, const std::string& timescale)
    : circuit_(circuit), file_(std::fopen(path.c_str(), "wb")), dumped_(circuit.nets().size(), Logic::X) {
    if (!file_) throw std::runtime_error("cannot write " + path);
    ids_.reserve(circuit.nets().size());
    for (std::size_t i = 0; i < circuit.nets().size(); ++i) ids_.push_back(vcd_id(i));
    buffer_.reserve(kFlushBytes + 4096);
    write_header(timescale);
}

VcdWriter::~VcdWriter() {
    if (file_) {
        flush();
        std::fclose(file_);
    }
}

void VcdWriter::write_header(const std::string& timescale) {
    std::time_t now = std::time(nullptr);
    char date[64];
    std::strftime(date, sizeof date, "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    buffer_ += "$date\n  " + std::string(date) + "\n$end\n";
    buffer_ += "$version\n  logicsim 0.1.0\n$end\n";
    buffer_ += "$timescale " + timescale + " $end\n";
    buffer_ += "$scope module " + circuit_.module_name + " $end\n";
    for (NetId n = 0; n < circuit_.nets().size(); ++n) {
        const Net& net = circuit_.nets()[n];
        if (net.is_constant) continue;
        buffer_ += "$var wire 1 " + ids_[n] + " " + vcd_reference(net.name) + " $end\n";
    }
    buffer_ += "$upscope $end\n$enddefinitions $end\n";
}

void VcdWriter::write_time(std::uint64_t time) {
    buffer_ += '#';
    buffer_ += std::to_string(time);
    buffer_ += '\n';
    last_time_ = time;
}

void VcdWriter::write_value(NetId net, Logic v) {
    buffer_ += to_char(v);
    buffer_ += ids_[net];
    buffer_ += '\n';
    dumped_[net] = v;
    ++changes_;
}

void VcdWriter::end_time_step(std::uint64_t time, const std::vector<NetId>& changed,
                              const std::vector<Logic>& values) {
    if (!started_) {
        // Initial values of every net: x before time 0, or the settled values if this is time 0.
        started_ = true;
        if (time != 0) {
            write_time(0);
            buffer_ += "$dumpvars\n";
            for (NetId n = 0; n < values.size(); ++n) {
                if (!circuit_.nets()[n].is_constant) write_value(n, Logic::X);
            }
            buffer_ += "$end\n";
        } else {
            write_time(0);
            buffer_ += "$dumpvars\n";
            for (NetId n = 0; n < values.size(); ++n) {
                if (!circuit_.nets()[n].is_constant) write_value(n, values[n]);
            }
            buffer_ += "$end\n";
            return;
        }
    }
    bool stamped = false;
    for (NetId n : changed) {
        if (values[n] == dumped_[n]) continue;  // changed and changed back within the step
        if (!stamped) {
            write_time(time);
            stamped = true;
        }
        write_value(n, values[n]);
    }
    if (buffer_.size() >= kFlushBytes) flush();
}

void VcdWriter::finish(std::uint64_t end_time) {
    if (end_time > last_time_) write_time(end_time);
    flush();
    std::fflush(file_);
}

void VcdWriter::flush() {
    if (buffer_.empty()) return;
    std::fwrite(buffer_.data(), 1, buffer_.size(), file_);
    bytes_ += buffer_.size();
    buffer_.clear();
}

}  // namespace logicsim
