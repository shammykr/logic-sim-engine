#include "logicsim/verilog_parser.hpp"

#include <cctype>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

namespace logicsim {
namespace {

enum class Tok { Ident, Number, Constant, Symbol, End };

struct Token {
    Tok kind;
    std::string text;
    int line;
};

class Lexer {
public:
    Lexer(const std::string& src, std::string file) : src_(src), file_(std::move(file)) {}

    std::vector<Token> run() {
        std::vector<Token> out;
        while (true) {
            skip_space_and_comments();
            if (pos_ >= src_.size()) break;
            char c = src_[pos_];
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                std::size_t start = pos_;
                while (pos_ < src_.size() && (std::isalnum(static_cast<unsigned char>(src_[pos_])) ||
                                              src_[pos_] == '_' || src_[pos_] == '$')) {
                    ++pos_;
                }
                out.push_back({Tok::Ident, src_.substr(start, pos_ - start), line_});
            } else if (c == '\\') {  // escaped identifier: up to the next whitespace
                std::size_t start = ++pos_;
                while (pos_ < src_.size() && !std::isspace(static_cast<unsigned char>(src_[pos_]))) ++pos_;
                if (pos_ == start) fail("empty escaped identifier");
                out.push_back({Tok::Ident, src_.substr(start, pos_ - start), line_});
            } else if (std::isdigit(static_cast<unsigned char>(c))) {
                std::size_t start = pos_;
                while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) ++pos_;
                if (pos_ < src_.size() && src_[pos_] == '\'') {  // sized constant such as 1'b0
                    ++pos_;
                    while (pos_ < src_.size() && std::isalnum(static_cast<unsigned char>(src_[pos_]))) ++pos_;
                    out.push_back({Tok::Constant, src_.substr(start, pos_ - start), line_});
                } else {
                    out.push_back({Tok::Number, src_.substr(start, pos_ - start), line_});
                }
            } else if (std::string("()[],;:#.=").find(c) != std::string::npos) {
                out.push_back({Tok::Symbol, std::string(1, c), line_});
                ++pos_;
            } else {
                fail(std::string("unexpected character '") + c + "'");
            }
        }
        out.push_back({Tok::End, "<end of file>", line_});
        return out;
    }

private:
    void skip_space_and_comments() {
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (c == '\n') {
                ++line_;
                ++pos_;
            } else if (std::isspace(static_cast<unsigned char>(c))) {
                ++pos_;
            } else if (src_.compare(pos_, 2, "//") == 0) {
                while (pos_ < src_.size() && src_[pos_] != '\n') ++pos_;
            } else if (src_.compare(pos_, 2, "/*") == 0) {
                std::size_t end = src_.find("*/", pos_ + 2);
                if (end == std::string::npos) fail("unterminated block comment");
                for (std::size_t i = pos_; i < end; ++i) line_ += src_[i] == '\n';
                pos_ = end + 2;
            } else if (c == '`') {  // compiler directives (`timescale ...): ignore the line
                while (pos_ < src_.size() && src_[pos_] != '\n') ++pos_;
            } else {
                break;
            }
        }
    }

    [[noreturn]] void fail(const std::string& msg) const {
        throw ParseError(file_ + ":" + std::to_string(line_) + ": " + msg);
    }

    const std::string& src_;
    std::string file_;
    std::size_t pos_ = 0;
    int line_ = 1;
};

std::optional<GateType> gate_keyword(const std::string& word) {
    static const std::map<std::string, GateType> kGates = {
        {"and", GateType::And},   {"or", GateType::Or},     {"nand", GateType::Nand},
        {"nor", GateType::Nor},   {"xor", GateType::Xor},   {"xnor", GateType::Xnor},
        {"not", GateType::Not},   {"buf", GateType::Buf},   {"dff", GateType::Dff},
        {"DFF", GateType::Dff},
    };
    auto it = kGates.find(word);
    if (it == kGates.end()) return std::nullopt;
    return it->second;
}

std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

class Parser {
public:
    Parser(std::vector<Token> tokens, std::string file) : toks_(std::move(tokens)), file_(std::move(file)) {}

    Circuit run() {
        expect_word("module");
        circuit_.module_name = expect_ident("module name");
        if (accept("(")) {
            if (!accept(")")) {
                do {
                    parse_port_item();
                } while (accept(","));
                expect(")");
            }
        }
        expect(";");
        while (!peek_word("endmodule")) {
            if (peek().kind == Tok::End) fail("missing endmodule");
            parse_item();
        }
        next();  // endmodule
        if (peek_word("module")) fail("only one module per file is supported (no hierarchy)");
        if (peek().kind != Tok::End) fail("unexpected '" + peek().text + "' after endmodule");

        for (const auto& port : port_order_) {
            if (!directions_.count(port)) fail("port " + port + " has no input/output declaration");
        }
        circuit_.finalize();
        return std::move(circuit_);
    }

private:
    // --- token helpers ----------------------------------------------------------------------
    const Token& peek() const { return toks_[pos_]; }
    const Token& next() { return toks_[pos_ < toks_.size() - 1 ? pos_++ : pos_]; }
    bool peek_symbol(const char* s) const { return peek().kind == Tok::Symbol && peek().text == s; }
    bool peek_word(const char* s) const { return peek().kind == Tok::Ident && peek().text == s; }
    bool accept(const char* s) {
        if (!peek_symbol(s)) return false;
        next();
        return true;
    }
    void expect(const char* s) {
        if (!accept(s)) fail(std::string("expected '") + s + "' but found '" + peek().text + "'");
    }
    void expect_word(const char* s) {
        if (!peek_word(s)) fail(std::string("expected '") + s + "' but found '" + peek().text + "'");
        next();
    }
    std::string expect_ident(const char* what) {
        if (peek().kind != Tok::Ident) fail(std::string("expected ") + what + " but found '" + peek().text + "'");
        return next().text;
    }
    long expect_number(const char* what) {
        if (peek().kind != Tok::Number) fail(std::string("expected ") + what + " but found '" + peek().text + "'");
        return std::stol(next().text);
    }
    [[noreturn]] void fail(const std::string& msg) const {
        throw ParseError(file_ + ":" + std::to_string(peek().line) + ": " + msg);
    }

    // --- declarations -----------------------------------------------------------------------
    using Range = std::pair<long, long>;  // [msb:lsb]

    std::optional<Range> parse_range() {
        if (!accept("[")) return std::nullopt;
        long msb = expect_number("range msb");
        expect(":");
        long lsb = expect_number("range lsb");
        expect("]");
        return Range{msb, lsb};
    }

    // Declares `name` (expanding vectors to one net per bit, named name[i]) and returns its nets.
    std::vector<NetId> declare(const std::string& name, const std::optional<Range>& range) {
        std::vector<NetId> nets;
        if (!range) {
            nets.push_back(circuit_.add_net(name));
            return nets;
        }
        widths_[name] = *range;
        long step = range->first >= range->second ? -1 : 1;
        for (long i = range->first;; i += step) {
            nets.push_back(circuit_.add_net(name + "[" + std::to_string(i) + "]"));
            if (i == range->second) break;
        }
        return nets;
    }

    void declare_direction(const std::string& dir, const std::string& name, const std::optional<Range>& range) {
        if (directions_.count(name)) fail("port " + name + " declared twice");
        directions_[name] = dir;
        for (NetId n : declare(name, range)) {
            if (dir == "input") circuit_.mark_input(n);
            else circuit_.mark_output(n);
        }
    }

    void parse_port_item() {
        if (peek_word("input") || peek_word("output")) {  // ANSI style: (input a, output [3:0] y)
            std::string dir = next().text;
            if (peek_word("wire")) next();
            auto range = parse_range();
            std::string name = expect_ident("port name");
            port_order_.push_back(name);
            declare_direction(dir, name, range);
            // Further names share the direction until the next direction keyword.
            while (peek_symbol(",") && toks_[pos_ + 1].kind == Tok::Ident &&
                   toks_[pos_ + 1].text != "input" && toks_[pos_ + 1].text != "output") {
                next();
                std::string more = expect_ident("port name");
                port_order_.push_back(more);
                declare_direction(dir, more, range);
            }
            return;
        }
        if (peek_word("inout")) fail("inout ports are not supported");
        port_order_.push_back(expect_ident("port name"));
    }

    void parse_item() {
        const Token& t = peek();
        if (t.kind != Tok::Ident) fail("unexpected '" + t.text + "'");
        if (t.text == "input" || t.text == "output") {
            std::string dir = next().text;
            if (peek_word("wire")) next();
            auto range = parse_range();
            do {
                declare_direction(dir, expect_ident("signal name"), range);
            } while (accept(","));
            expect(";");
        } else if (t.text == "wire") {
            next();
            auto range = parse_range();
            do {
                declare(expect_ident("wire name"), range);
            } while (accept(","));
            expect(";");
        } else if (auto type = gate_keyword(t.text)) {
            next();
            parse_gate_statement(*type);
        } else if (t.text == "inout") {
            fail("inout ports are not supported");
        } else if (t.text == "assign" || t.text == "always" || t.text == "initial" || t.text == "reg") {
            fail("'" + t.text + "' is behavioural Verilog; only structural netlists are supported");
        } else {
            fail("unknown gate or keyword '" + t.text + "' (module instances are not supported)");
        }
    }

    // --- gate instances ---------------------------------------------------------------------
    NetId parse_net_ref() {
        if (peek().kind == Tok::Constant) {
            std::string c = next().text;
            char v = static_cast<char>(std::tolower(static_cast<unsigned char>(c.back())));
            if (c.size() < 4 || std::tolower(static_cast<unsigned char>(c[c.size() - 2])) != 'b' ||
                (v != '0' && v != '1' && v != 'x')) {
                fail("unsupported constant " + c + " (use 1'b0, 1'b1 or 1'bx)");
            }
            return circuit_.constant_net(v == '0' ? Logic::Zero : v == '1' ? Logic::One : Logic::X);
        }
        std::string name = expect_ident("net name");
        if (accept("[")) {
            long bit = expect_number("bit index");
            expect("]");
            auto w = widths_.find(name);
            if (w == widths_.end()) fail(name + " is not a vector");
            long lo = std::min(w->second.first, w->second.second);
            long hi = std::max(w->second.first, w->second.second);
            if (bit < lo || bit > hi) fail("bit " + std::to_string(bit) + " is outside " + name + "'s range");
            return circuit_.add_net(name + "[" + std::to_string(bit) + "]");
        }
        if (widths_.count(name)) fail(name + " is a vector; select a bit, e.g. " + name + "[0]");
        return circuit_.add_net(name);  // implicit wire if undeclared, as in Verilog
    }

    std::uint32_t parse_delay() {
        if (!accept("#")) return 0;
        bool paren = accept("(");
        long d = expect_number("delay");
        if (paren) expect(")");
        if (d < 0) fail("negative delay");
        return static_cast<std::uint32_t>(d);
    }

    void parse_gate_statement(GateType type) {
        std::uint32_t delay = parse_delay();
        do {
            std::string name;
            if (peek().kind == Tok::Ident) name = next().text;
            if (peek_symbol("[")) fail("instance arrays are not supported");
            if (name.empty()) name = std::string(gate_type_name(type)) + "$" + std::to_string(++anonymous_);
            if (!instance_names_.insert(name).second) fail("duplicate instance name " + name);
            expect("(");
            if (type == GateType::Dff) parse_dff(name, delay);
            else parse_primitive(type, name, delay);
            expect(")");
        } while (accept(","));
        expect(";");
    }

    void parse_primitive(GateType type, const std::string& name, std::uint32_t delay) {
        if (peek_symbol(".")) fail("named port connections are only supported on dff");
        std::vector<NetId> conns;
        do {
            conns.push_back(parse_net_ref());
        } while (accept(","));
        std::size_t inputs = conns.size() - 1;
        bool single = type == GateType::Not || type == GateType::Buf;
        if (conns.size() < 2 || (single && inputs != 1)) {
            fail(std::string(gate_type_name(type)) + " " + name + ": expected (output, input" +
                 (single ? ")" : ", input...)"));
        }
        circuit_.add_gate(type, name, conns[0], std::vector<NetId>(conns.begin() + 1, conns.end()), delay);
    }

    // dff name (Q, D, CLK [, RST])  or  dff name (.Q(q), .D(d), .CLK(clk) [, .RST(rst)])
    void parse_dff(const std::string& name, std::uint32_t delay) {
        NetId q = kNoNet, d = kNoNet, clk = kNoNet, rst = kNoNet;
        if (peek_symbol(".")) {
            do {
                expect(".");
                std::string pin = upper(expect_ident("pin name"));
                expect("(");
                NetId n = parse_net_ref();
                expect(")");
                NetId* slot = pin == "Q" ? &q
                            : pin == "D" ? &d
                            : (pin == "CLK" || pin == "CK" || pin == "C") ? &clk
                            : (pin == "RST" || pin == "R" || pin == "RESET") ? &rst
                            : nullptr;
                if (!slot) fail("dff " + name + ": unknown pin ." + pin + " (Q, D, CLK, RST)");
                if (*slot != kNoNet) fail("dff " + name + ": pin ." + pin + " connected twice");
                *slot = n;
            } while (accept(","));
        } else {
            std::vector<NetId> conns;
            do {
                conns.push_back(parse_net_ref());
            } while (accept(","));
            if (conns.size() != 3 && conns.size() != 4) fail("dff " + name + ": expected (Q, D, CLK [, RST])");
            q = conns[0];
            d = conns[1];
            clk = conns[2];
            if (conns.size() == 4) rst = conns[3];
        }
        if (q == kNoNet || d == kNoNet || clk == kNoNet) fail("dff " + name + ": Q, D and CLK must be connected");
        std::vector<NetId> ins = {d, clk};
        if (rst != kNoNet) ins.push_back(rst);
        circuit_.add_gate(GateType::Dff, name, q, ins, delay);
    }

    std::vector<Token> toks_;
    std::string file_;
    std::size_t pos_ = 0;
    Circuit circuit_;
    std::vector<std::string> port_order_;
    std::map<std::string, std::string> directions_;
    std::map<std::string, Range> widths_;
    std::set<std::string> instance_names_;
    unsigned anonymous_ = 0;
};

}  // namespace

Circuit parse_verilog(const std::string& source, const std::string& filename) {
    Lexer lexer(source, filename);
    Parser parser(lexer.run(), filename);
    return parser.run();
}

Circuit parse_verilog_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw ParseError("cannot open " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return parse_verilog(ss.str(), path);
}

}  // namespace logicsim
