#include <unordered_map>

#include "gcc-plugin.h"
#include "plugin.h"
#include "plugin-version.h"

#include "context.h"
#include "diagnostic.h"
#include "stringpool.h"
#include "tree.h"
#include "tree-pass.h"
#include "basic-block.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "gimple-pretty-print.h"
#include "tree-pretty-print.h"

#include "func_walker.hh"

// Define plugin information
int plugin_is_GPL_compatible; // Set to 1 for GPL compatibility
const char *plugin_version = "1.0";

namespace lock_checker {

static const struct pass_data my_pass_data = {
    .type = GIMPLE_PASS,
    .name = "checks_stuff",
    .optinfo_flags = OPTGROUP_NONE,
    .tv_id = TV_NONE,
    .properties_provided = PROP_ssa | PROP_cfg,
};

static tree follow_ssa(tree v) {
    while (v != NULL && TREE_CODE(v) == SSA_NAME) {
        auto* stmt = SSA_NAME_DEF_STMT(v);
        if (gimple_code(stmt) == GIMPLE_ASSIGN && gimple_assign_rhs_code(stmt) == SSA_NAME) {
            v = gimple_assign_rhs1(stmt);
        } else {
            return v;
        }
    }

    return v;
}

static tree follow_var_decl(tree v) {
    if (v == nullptr) {
        return nullptr;
    }
    if (TREE_CODE(v) == VAR_DECL) {
        return v;
    }
    if (TREE_CODE(v) == SSA_NAME) {
        auto* stmt = SSA_NAME_DEF_STMT(v);
        if (gimple_code(stmt) == GIMPLE_ASSIGN && gimple_assign_rhs_code(stmt) == VAR_DECL) {
            return gimple_assign_rhs1(stmt);
        }
    } 
    return nullptr;
}

template <typename T> void dump_vals(T& t) {
    fprintf(stderr, "[");
    for (auto v: t) {
        fprintf(stderr, "%d ", v);
    }
    fprintf(stderr, "]");
}

using possible_vals = std::optional<std::pair<std::vector<int64_t>, std::vector<int>>>;

template <typename F> possible_vals merge_vals(possible_vals& a, possible_vals& b, F op) {
    if (!a.has_value()) {
        //fprintf(stderr, "merge_vals a empty\n");
        return std::nullopt;
    }
    if (!b.has_value()) {
        //fprintf(stderr, "merge_vals b empty\n");
        return std::nullopt;
    }
    auto& [a_vals, a_list] = *a;
    auto& [b_vals, b_list] = *b;
    if (a_vals.size() != (size_t)(1 << a_list.size())) {
        fprintf(stderr, "ERROR: possibility list has the wrong size");
        return std::nullopt;
    }
    if (b_vals.size() != (size_t)(1 << b_list.size())) {
        fprintf(stderr, "ERROR: possibility list has the wrong size");
        return std::nullopt;
    }

    std::vector<int> merged_list(a_list);
    std::vector<int> b_idx_map;
    for (auto &b_l: b_list) {
        bool found = false;
        for (size_t idx = 0; idx < merged_list.size(); idx++) {
            if (b_l == merged_list[idx]) {
                b_idx_map.push_back(idx);
                found = true;
                break;
            }
        }
        if (!found) {
            merged_list.push_back(b_l);
            b_idx_map.push_back(b_list.size() - 1);
        }
    }
    std::vector<int64_t> merged_vals((1 << merged_list.size()), 0);
    //fprintf(stderr, "a sz %d b sz %d merge_vals sz %d\n", a_vals.size(), b_vals.size(), merged_vals.size());
    const int a_mask = (1 << a_list.size()) - 1;
    for (size_t i = 0; i < merged_vals.size(); i++) {
        int a_idx = i & a_mask;
        int b_idx = 0;
        for (size_t j = 0; j < b_list.size(); j++) {
            int bit = 1 << b_idx_map[j];
            if ((i & bit) != 0) {
                b_idx |= (1 << j);
            }
        }
        merged_vals[i] = op(a_vals[a_idx], b_vals[b_idx]);
    }

    return std::pair<std::vector<int64_t>, std::vector<int>> {
        merged_vals,
        merged_list
    };
}

// attempts to calculate the possible results for a expression, assuming the expression
// consists only of constants and references to lock calls
// returns nullopt if there is anything else in the expression
static std::optional<std::pair<std::vector<int64_t>, std::vector<int>>> calc_vals(tree v, const std::unordered_map<gimple*, int>& lock_calls) {
    std::vector<int64_t> results;
    //fprintf(stderr, "calc vals %p\n", v);

    // i'm pretty sure by this pass everything's been lowered to gimple,
    // so it should only be ssa assignments
    tree base = follow_ssa(v);
    if (TREE_CODE(v) == SSA_NAME) {
        auto* stmt = SSA_NAME_DEF_STMT(v);
        if (auto it = lock_calls.find(stmt); it != lock_calls.end()) {
            //fprintf(stderr, "found lock\n");
            // we found it
            return std::pair<std::vector<int64_t>, std::vector<int>>{
                std::vector<int64_t>{0, 1}, // pdFAIL, pdPASS
                std::vector<int>{it->second}
            };
        }
        if (gimple_code(stmt) == GIMPLE_ASSIGN) {
            auto assign = as_a<gassign*>(stmt);
            auto subcode = gimple_assign_rhs_code(stmt);
            if (subcode == INTEGER_CST) {
                //fprintf(stderr, "found const\n");
                return std::pair<std::vector<int64_t>, std::vector<int>>{
                    std::vector<int64_t>{tree_to_shwi(gimple_assign_rhs1(stmt))},
                    std::vector<int>{}
                };
            } else if (subcode == PLUS_EXPR) {
                //fprintf(stderr, "+ merging %p %p\n", gimple_assign_rhs1(stmt), gimple_assign_rhs2(stmt));
                auto left = calc_vals(gimple_assign_rhs1(stmt), lock_calls);
                auto right = calc_vals(gimple_assign_rhs2(stmt), lock_calls); 
                return merge_vals(left, right, [](int64_t a, int64_t b) {
                           return a + b;
                           });
            } else if (subcode == MINUS_EXPR) {
                //fprintf(stderr, "- merging %p %p\n", gimple_assign_rhs1(stmt), gimple_assign_rhs2(stmt));
                auto left = calc_vals(gimple_assign_rhs1(stmt), lock_calls);
                auto right = calc_vals(gimple_assign_rhs2(stmt), lock_calls); 
                return merge_vals(left, right, [](int64_t a, int64_t b) {
                           return a + b;
                           });
            } else if (subcode == EQ_EXPR) {
                //fprintf(stderr, "eq merging %p %p\n", gimple_assign_rhs1(stmt), gimple_assign_rhs2(stmt));
                auto left = calc_vals(gimple_assign_rhs1(stmt), lock_calls);
                auto right = calc_vals(gimple_assign_rhs2(stmt), lock_calls); 
                return merge_vals(left, right, [](int64_t a, int64_t b) {
                           return a == b;
                           });
            } else if (subcode == NE_EXPR) {
                //fprintf(stderr, "ne merging %p %p\n", gimple_assign_rhs1(stmt), gimple_assign_rhs2(stmt));
                auto left = calc_vals(gimple_assign_rhs1(stmt), lock_calls);
                auto right = calc_vals(gimple_assign_rhs2(stmt), lock_calls); 
                return merge_vals(left, right, [](int64_t a, int64_t b) {
                           return a != b;
                           });
            } else if (subcode == SSA_NAME) {
                //fprintf(stderr, "found ssa\n");
                return calc_vals(gimple_assign_rhs1(stmt), lock_calls);
            } else if (subcode == VAR_DECL) {
                return std::nullopt;
            } else {
                fprintf(stderr, "W: UNKNOWN subcode in calc_vals %d for %p\n", subcode, v);
            }
        } else {
            fprintf(stderr, "W: UNEXPECTED gimple code %d\n", gimple_code(stmt));
        }
        return std::nullopt;
    } else if (TREE_CODE(v) == INTEGER_CST) {
            //fprintf(stderr, "found const\n");
            return std::pair<std::vector<int64_t>, std::vector<int>>{
                std::vector<int64_t>{tree_to_shwi(v)},
                std::vector<int>{}
            };
    } else {
        fprintf(stderr, "W: UNKNOWN tree code in calc_vals %d for %p\n", TREE_CODE(v), v);
    }
    return std::nullopt;
}

static bool match_call(gcall* stmt, const char* fname, int nargs) {
    tree fn = gimple_call_fn(stmt);
    if (TREE_CODE(fn) == ADDR_EXPR) {
        auto inner = TREE_OPERAND(fn, 0);
        if (TREE_CODE(inner) == FUNCTION_DECL) {
            const char* name = IDENTIFIER_POINTER(DECL_NAME(inner));
            if (strcmp(name, fname) == 0) {
                if (gimple_call_num_args(stmt) == nargs) {
                    return true;
                } else {
                    fprintf(stderr, "W: found matching function call for %s with incorrect number of arguments\n");
                }
            }
        }
    }
    return false;
}

enum class error {
    kDoubleLock,
    kGiveWithoutTakeFirst,
    kTakeWithoutGive
};

struct GccAdapter {
    using FuncHandler = tree;
    using Location = location_t;
};

// two passes through all the basic blocks,
// first to find all the locks in use and propagate the
// lock state to each basic block, and then
// second to generate the warnings at the right location
// by replaying each block given its starting state
struct function_checker {
    std::unordered_map<tree, std::shared_ptr<lock_checker::func<GccAdapter>>> functions; // decl to 
    std::unordered_map<tree, std::vector<tree>> used_byte;
};
// invariants to reinforce:
// - output lock state must match input state
// - don't take blocking semaphore twice in a row (deadlock)

// assumptions:
// - if it's not currently locked by this task, any nonblocking lock can fail due to an external task

struct pass: public gimple_opt_pass {
public:
    pass(gcc::context* ctx): gimple_opt_pass(my_pass_data, ctx) {}

    virtual unsigned int execute(function* f) override {
        warning(0, "in function %s", IDENTIFIER_POINTER(DECL_NAME(f->decl)));

        std::unordered_map<gimple*, int> lock_idx; // lock idx
        std::unordered_map<tree, int> lock_decl_idx; // declaration linked with a lock
        int num_locks = 0;
        int num_calls = 0;

        basic_block bb;
        FOR_ALL_BB_FN(bb, f) {
            lock_checker::bb<GccAdapter> cur_bb = {};

            gimple_bb_info* bb_info = &bb->il.gimple;
            fprintf(stderr, "bb start %d\n", bb->index);
            //print_gimple_seq(stderr, bb_info->seq, 0, (dump_flags_t)0);
            gimple_stmt_iterator gsi;
            for (gsi = gsi_start(bb_info->seq); !gsi_end_p(gsi); gsi_next(&gsi)) {
                gimple* gs = gsi_stmt(gsi);
                //fprintf(stderr, "\tstmt %p %d code %d\n\t", gs, c, gs->code);
                pretty_printer pp;
                pp_needs_newline(&pp) = true;
                pp.set_output_stream(stderr);
                pp_gimple_stmt_1(&pp, gs, 0, TDF_RAW);
                pp_newline_and_flush(&pp);

                if (gimple_code(gs) == GIMPLE_CALL) {
                    auto* stmt = as_a<gcall*>(gs);
                    if (match_call(stmt, "lock", 2)) {
                        fprintf(stderr, "\tfound lock! %p\n", stmt);

                        auto rhs = gimple_call_arg(stmt, 0);
                        auto real_rhs = follow_var_decl(follow_ssa(rhs));

                        if (real_rhs != nullptr) {
                            auto decl_id = DECL_NAME(real_rhs);
                            if (auto it = lock_decl_idx.find(decl_id); it == lock_decl_idx.end()) {
                                fprintf(stderr, "\t\tfound new lock for %p, decl_id %d\n", decl_id, num_locks);
                                lock_idx[stmt] = num_locks;
                                lock_decl_idx[decl_id] = num_locks;
                                num_locks++;
                            } else {
                                fprintf(stderr, "\t\tfound existing lock %d\n", it->second);
                            }
                        }

                        int cur_lock = lock_idx[stmt];

                        auto delay = gimple_call_arg(stmt, 1);
                        auto delay_val = calc_vals(delay, lock_idx);
                        if (!delay_val) {
                            // TODO post warning
                            // if we can't convert the delay to a constant you're doing something terribly wrong
                        } else {
                            auto &[delay_vals, delay_list] = *delay_val;
                            if (delay_vals.size() > 1) {
                                // TODO post warning
                            } else {
                            }
                        }
                    } else if (match_call(stmt, "unlock", 1)) {
                        auto rhs = gimple_call_arg(stmt, 0);
                        auto real_rhs = follow_var_decl(follow_ssa(rhs));
                        if (real_rhs != nullptr) {
                            auto decl_id = DECL_NAME(real_rhs);
                            if (auto it = lock_decl_idx.find(decl_id); it != lock_decl_idx.end()) {
                                fprintf(stderr, "\t\tmatched with lock %d\n", it->second);
                            }
                        }
                    }
                }
                if (gs->code == GIMPLE_COND) {
                    fprintf(stderr, "\tfound cond\n");
                    gcond* stmt = as_a<gcond*>(gs);

                    tree lhs = gimple_cond_lhs(stmt);
                    tree rhs = gimple_cond_rhs(stmt);
                    auto maybe_lhs = calc_vals(lhs, lock_idx);
                    auto maybe_rhs = calc_vals(rhs, lock_idx);

                    auto code = gimple_cond_code(stmt);
                    if (code == EQ_EXPR) {
                        auto result = merge_vals(maybe_lhs, maybe_rhs, [](int64_t a, int64_t b) -> bool {
                            return a == b;
                        });
                        if (result) {
                            auto &[result_vals, result_list] = *result;
                            fprintf(stderr, "cond vals ");
                            dump_vals(result_vals);
                            fprintf(stderr, "\n");
                        }
                    } else if (code == NE_EXPR) {
                        auto result = merge_vals(maybe_lhs, maybe_rhs, [](int64_t a, int64_t b) -> bool {
                            return a != b;
                        });
                        if (result) {
                            auto &[result_vals, result_list] = *result;
                            fprintf(stderr, "cond vals ");
                            dump_vals(result_vals);
                            fprintf(stderr, "\n");
                        }
                    } else {
                        fprintf(stderr, "\t\tUNKNOWN cond code %d\n", code);
                    }
                }
            }
            edge e;
            edge_iterator ei;
            FOR_EACH_EDGE(e, ei, bb->succs) {
                basic_block dest = e->dest;
                fprintf(stderr, "-> %d %04x\n", dest->index, e->flags);
            }

        }
        return 0;
    }
};

}

// A simple callback function
static void my_callback(void *gcc_data, void *user_data) {
    // This function will be called at a specific point during compilation
    // You can add your custom logic here, e.g., print a message.
    fprintf(stderr, "GCC Plugin: My callback was invoked!\n");
}


// Plugin initialization function
int plugin_init(plugin_name_args *plugin_info, plugin_gcc_version *version) {
    // Check for GCC version compatibility
    if (!plugin_default_version_check(version, &gcc_version)) {
        return 1; // Incompatible version
    }

    struct register_pass_info pass_info = {
        .pass = new lock_checker::pass(g),
        .reference_pass_name = "nrv",
        .ref_pass_instance_number = 1,
        .pos_op = PASS_POS_INSERT_AFTER,
    };

    // Register the callback
    // Here, we register it for the 'PLUGIN_START_UNIT' event,
    // which occurs at the beginning of compiling a translation unit.
    register_callback(plugin_info->base_name, PLUGIN_START_UNIT, my_callback, NULL);
    register_callback(plugin_info->base_name, PLUGIN_PASS_MANAGER_SETUP, NULL, &pass_info);

    fprintf(stderr, "GCC Plugin: My plugin loaded successfully!\n");
    return 0; // Success
}
