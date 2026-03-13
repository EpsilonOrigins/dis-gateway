#include "rule_engine.hpp"
#include "field_accessor.hpp"

#include <cctype>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

// ---------------------------------------------------------------------------
// Parsing helpers
// ---------------------------------------------------------------------------
namespace {

// Split a line into whitespace-separated tokens, stopping at '#'
std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> tokens;
    std::istringstream iss(line);
    std::string tok;
    while (iss >> tok) {
        if (!tok.empty() && tok[0] == '#') break; // rest is comment
        tokens.push_back(tok);
    }
    return tokens;
}

// Parse a numeric token.  Accepts integer or floating-point.
double parse_num(const std::string& s) {
    try { return std::stod(s); }
    catch (...) {
        throw std::runtime_error("Expected a number, got: " + s);
    }
}

} // anonymous namespace

// ===========================================================================
// RuleEngine::parse()
// ===========================================================================
void RuleEngine::parse(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open())
        throw std::runtime_error("Cannot open rules file: " + path);

    // We support a single level of if/endif nesting.
    // Outer blocks (no active condition) go into a "global" block; when an
    // "if" is encountered we open a new block with conditions attached.

    RuleBlock global_block;  // collects unconditional rules
    std::vector<RuleBlock*> stack = {&global_block};

    std::string line;
    int lineno = 0;

    auto err = [&](const std::string& msg) {
        throw std::runtime_error(path + ":" + std::to_string(lineno) + ": " + msg);
    };

    auto need = [&](const std::vector<std::string>& toks, std::size_t n) {
        if (toks.size() < n)
            err("Too few tokens for '" + toks[0] + "' (expected " +
                std::to_string(n) + ")");
    };

    while (std::getline(f, line)) {
        ++lineno;
        auto toks = tokenize(line);
        if (toks.empty()) continue;

        const std::string& cmd = toks[0];

        // ---- if <field> [op] <val...> ----------------------------------------
        if (cmd == "if") {
            need(toks, 3);
            Condition c;
            c.field = toks[1];

            std::size_t val_start = 2;
            std::string op_tok   = toks[2];

            if (op_tok == "==" || op_tok == "=") {
                c.op = CondOp::EQ;  val_start = 3; need(toks, 4);
            } else if (op_tok == "!=" || op_tok == "<>") {
                c.op = CondOp::NEQ; val_start = 3; need(toks, 4);
            } else if (op_tok == "in") {
                c.op = CondOp::IN;  val_start = 3; need(toks, 4);
            } else {
                // bare value – treat as ==
                c.op = CondOp::EQ;  val_start = 2;
            }

            for (std::size_t i = val_start; i < toks.size(); ++i)
                c.values.push_back(parse_num(toks[i]));

            // Open a new block with this condition
            blocks_.push_back({});
            blocks_.back().conditions.push_back(c);
            stack.push_back(&blocks_.back());
        }

        // ---- endif -----------------------------------------------------------
        else if (cmd == "endif") {
            if (stack.size() < 2)
                err("'endif' without matching 'if'");
            stack.pop_back();
        }

        // ---- remap <field> <old>=<new> ... ----------------------------------
        else if (cmd == "remap") {
            need(toks, 3);
            Action a;
            a.op    = ActionOp::REMAP;
            a.field = toks[1];
            for (std::size_t i = 2; i < toks.size(); ++i) {
                auto eq = toks[i].find('=');
                if (eq == std::string::npos)
                    err("remap mapping must be old=new, got: " + toks[i]);
                int64_t from = static_cast<int64_t>(parse_num(toks[i].substr(0, eq)));
                double  to   = parse_num(toks[i].substr(eq + 1));
                a.remap[from] = to;
            }
            stack.back()->actions.push_back(std::move(a));
        }

        // ---- offset <field> <delta> -----------------------------------------
        else if (cmd == "offset") {
            need(toks, 3);
            Action a;
            a.op      = ActionOp::OFFSET;
            a.field   = toks[1];
            a.num_val = parse_num(toks[2]);
            stack.back()->actions.push_back(std::move(a));
        }

        // ---- set <field> <value> --------------------------------------------
        else if (cmd == "set") {
            need(toks, 3);
            Action a;
            a.op      = ActionOp::SET;
            a.field   = toks[1];
            a.num_val = parse_num(toks[2]);
            stack.back()->actions.push_back(std::move(a));
        }

        // ---- set_str <field> <value...> ------------------------------------
        // value may contain spaces (rest of line after field)
        else if (cmd == "set_str") {
            need(toks, 3);
            Action a;
            a.op      = ActionOp::SET_STR;
            a.field   = toks[1];
            // Rejoin remaining tokens as the string value
            std::string val;
            for (std::size_t i = 2; i < toks.size(); ++i) {
                if (i > 2) val += ' ';
                val += toks[i];
            }
            // Strip optional surrounding quotes
            if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
                val = val.substr(1, val.size() - 2);
            a.str_val = val;
            stack.back()->actions.push_back(std::move(a));
        }

        // ---- drop -----------------------------------------------------------
        else if (cmd == "drop") {
            stack.back()->actions.push_back({ActionOp::DROP, {}, {}, {}, {}});
        }

        // ---- passthrough ---------------------------------------------------
        else if (cmd == "passthrough") {
            stack.back()->actions.push_back({ActionOp::PASSTHROUGH, {}, {}, {}, {}});
        }

        // ---- log <message...> ----------------------------------------------
        else if (cmd == "log") {
            Action a;
            a.op = ActionOp::LOG;
            std::string msg;
            for (std::size_t i = 1; i < toks.size(); ++i) {
                if (i > 1) msg += ' ';
                msg += toks[i];
            }
            if (msg.size() >= 2 && msg.front() == '"' && msg.back() == '"')
                msg = msg.substr(1, msg.size() - 2);
            a.str_val = msg;
            stack.back()->actions.push_back(std::move(a));
        }

        // ---- Unknown keyword -----------------------------------------------
        else if (cmd[0] != '#') {
            err("Unknown directive: " + cmd);
        }
    }

    if (stack.size() > 1)
        err("Unclosed 'if' block at end of file");

    // Move global (unconditional) block to the front
    blocks_.insert(blocks_.begin(), std::move(global_block));
}

// ===========================================================================
// Constructor
// ===========================================================================
RuleEngine::RuleEngine(const std::string& rules_file, Options opts)
    : opts_(std::move(opts))
{
    parse(rules_file);
}

// ===========================================================================
// Condition evaluation
// ===========================================================================
bool RuleEngine::eval_condition(const Condition& c, const dis::AnyPdu& pdu) const {
    auto val_opt = FieldAccessor::get(pdu, c.field);
    if (!val_opt) return false; // field not present on this PDU type

    double actual = *val_opt;

    switch (c.op) {
        case CondOp::EQ:
            return std::fabs(actual - c.values[0]) < 0.5;
        case CondOp::NEQ:
            return std::fabs(actual - c.values[0]) >= 0.5;
        case CondOp::IN:
            for (double expected : c.values)
                if (std::fabs(actual - expected) < 0.5) return true;
            return false;
    }
    return false;
}

// ===========================================================================
// Block application  (returns false = drop)
// ===========================================================================
bool RuleEngine::apply_block(const RuleBlock& b, dis::AnyPdu& pdu) const {
    // Check conditions
    for (const auto& c : b.conditions)
        if (!eval_condition(c, pdu)) return true; // conditions not met – skip

    // Apply actions
    for (const auto& a : b.actions) {
        switch (a.op) {
            case ActionOp::DROP:
                return false;

            case ActionOp::PASSTHROUGH:
                break; // explicit no-op

            case ActionOp::LOG:
                do_log(a.str_val);
                break;

            case ActionOp::SET:
                if (!FieldAccessor::set(pdu, a.field, a.num_val)) {
                    do_log("Warning: set failed for field '" + a.field + "'");
                }
                break;

            case ActionOp::SET_STR:
                if (!FieldAccessor::set_str(pdu, a.field, a.str_val)) {
                    do_log("Warning: set_str failed for field '" + a.field + "'");
                }
                break;

            case ActionOp::OFFSET: {
                auto cur = FieldAccessor::get(pdu, a.field);
                if (cur) {
                    FieldAccessor::set(pdu, a.field, *cur + a.num_val);
                } else {
                    do_log("Warning: offset failed for field '" + a.field + "'");
                }
                break;
            }

            case ActionOp::REMAP: {
                auto cur = FieldAccessor::get(pdu, a.field);
                if (cur) {
                    auto key = static_cast<int64_t>(std::round(*cur));
                    auto it  = a.remap.find(key);
                    if (it != a.remap.end())
                        FieldAccessor::set(pdu, a.field, it->second);
                }
                break;
            }
        }
    }
    return true; // not dropped
}

// ===========================================================================
// transform()
// ===========================================================================
std::optional<dis::AnyPdu> RuleEngine::transform(const dis::AnyPdu& pdu) {
    dis::AnyPdu mut = pdu;

    for (const auto& b : blocks_) {
        try {
            if (!apply_block(b, mut))
                return std::nullopt; // dropped
        } catch (const std::exception& e) {
            do_log(std::string("Rule error: ") + e.what());
            if (!opts_.passthrough_on_error) return std::nullopt;
            return pdu; // passthrough original on error
        }
    }

    return mut;
}

void RuleEngine::do_log(const std::string& msg) const {
    if (log_cb_) log_cb_(msg);
    else         std::cerr << "[rules] " << msg << "\n";
}
