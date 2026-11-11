#include <iostream>

#include "logicsim/verilog_parser.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: logicsim <netlist.v>\n";
        return 2;
    }
    try {
        auto c = logicsim::parse_verilog_file(argv[1]);
        std::cout << "module " << c.module_name << ": " << c.nets().size() << " nets, " << c.gates().size()
                  << " gates, depth " << c.logic_depth() << "\n";
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
