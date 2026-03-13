#pragma once
#include "dis/pdus.hpp"
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

struct RuleEngineOptions {
    bool passthrough_on_error = true; // on runtime error, forward unchanged
};

// ---------------------------------------------------------------------------
// RuleEngine
//
// Loads a plain-text rules file and applies it to each PDU.
//
// Rules file format  (see config/example_rules.conf):
//
//   # comment
//   # blank lines ignored
//
//   # Unconditional actions (applied to every PDU):
//   remap  <field>  <old>=<new>  [<old>=<new> ...]
//   offset <field>  <delta>
//   set    <field>  <value>
//   set_str <field> <value>
//   drop
//   passthrough                       # explicit no-op
//   log    <message>
//
//   # Conditional blocks (conditions are ANDed):
//   if <field> [==|!=|in] <val> [<val2> ...]
//       <actions>
//   endif
//
//   # Shorthand condition operators:
//   #   if pdu_type == 1          (single equality)
//   #   if exercise_id != 99      (single inequality)
//   #   if pdu_type in 1 2 3      (membership)
//   #   if pdu_type 1             (bare value = equality shorthand)
//
// Rules are evaluated top-to-bottom.  A "drop" inside a matching block
// immediately drops the PDU.  All other rules continue to run unless a drop
// is encountered.
// ---------------------------------------------------------------------------
class RuleEngine {
public:
    using Options = RuleEngineOptions;

    using LogCallback = std::function<void(const std::string&)>;

    RuleEngine(const std::string& rules_file, Options opts = Options{});

    std::optional<dis::AnyPdu> transform(const dis::AnyPdu& pdu);

    void set_log_callback(LogCallback cb) { log_cb_ = std::move(cb); }

private:
    Options     opts_;
    LogCallback log_cb_;

    // ---- Internal IR -------------------------------------------------------
    enum class CondOp  { EQ, NEQ, IN };
    enum class ActionOp { SET, SET_STR, REMAP, OFFSET, DROP, PASSTHROUGH, LOG };

    struct Condition {
        std::string         field;
        CondOp              op   = CondOp::EQ;
        std::vector<double> values;
    };

    struct Action {
        ActionOp                       op;
        std::string                    field;
        double                         num_val  = 0.0;
        std::string                    str_val;
        std::map<int64_t, double>      remap;
    };

    struct RuleBlock {
        std::vector<Condition> conditions; // all must match (AND); empty = always
        std::vector<Action>    actions;
    };

    std::vector<RuleBlock> blocks_;

    // ---- Parsing -----------------------------------------------------------
    void parse(const std::string& path);

    // ---- Evaluation --------------------------------------------------------
    bool eval_condition(const Condition& c, const dis::AnyPdu& pdu) const;
    // Returns false if DROP was triggered.
    bool apply_block(const RuleBlock& b, dis::AnyPdu& pdu) const;

    void do_log(const std::string& msg) const;
};
