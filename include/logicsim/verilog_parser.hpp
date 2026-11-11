// Parser for a structural subset of Verilog-2001 (see README "Supported Verilog subset").
#pragma once

#include <string>

#include "logicsim/circuit.hpp"

namespace logicsim {

class ParseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Parses one module and returns the finalized circuit. Throws ParseError on syntax errors and
// NetlistError on structural errors (multiple drivers, combinational loops, ...).
Circuit parse_verilog(const std::string& source, const std::string& filename = "<input>");
Circuit parse_verilog_file(const std::string& path);

}  // namespace logicsim
