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

// Define plugin information
int plugin_is_GPL_compatible; // Set to 1 for GPL compatibility
const char *plugin_version = "1.0";

// A simple callback function
static void my_callback(void *gcc_data, void *user_data) {
    // This function will be called at a specific point during compilation
    // You can add your custom logic here, e.g., print a message.
    fprintf(stderr, "GCC Plugin: My callback was invoked!\n");
}

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
        if (gimple_code(stmt) == GIMPLE_ASSIGN) {
            v = gimple_assign_rhs1(stmt);
        } else {
            return v;
        }
    }

    return v;
}

struct my_custom_pass: public gimple_opt_pass {
public:
    my_custom_pass(gcc::context* ctx): gimple_opt_pass(my_pass_data, ctx) {}

    virtual unsigned int execute(function* f) override {
        warning(0, "in function %s", IDENTIFIER_POINTER(DECL_NAME(f->decl)));
        std::unordered_map<gimple*, int> lock_idx;

        basic_block bb;
        int c = 0;
        FOR_ALL_BB_FN(bb, f) {
            gimple_bb_info* bb_info = &bb->il.gimple;
            fprintf(stderr, "bb start %d\n", bb->index);
            //print_gimple_seq(stderr, bb_info->seq, 0, (dump_flags_t)0);
            gimple_stmt_iterator gsi;
            for (gsi = gsi_start(bb_info->seq); !gsi_end_p(gsi); gsi_next(&gsi)) {
                gimple* gs = gsi_stmt(gsi);
                fprintf(stderr, "\tstmt %p %d code %d\n\t", gs, c, gs->code);
                pretty_printer pp;
                pp_needs_newline(&pp) = true;
                pp.set_output_stream(stderr);
                pp_gimple_stmt_1(&pp, gs, 0, TDF_RAW);
                pp_newline_and_flush(&pp);

                if (gimple_code(gs) == GIMPLE_CALL) {
                    auto* stmt = as_a<gcall*>(gs);
                    tree fn = gimple_call_fn(stmt);
                    if (TREE_CODE(fn) == ADDR_EXPR) {
                        auto inner = TREE_OPERAND(fn, 0);
                        if (TREE_CODE(inner) == FUNCTION_DECL) {
                            const char* name = IDENTIFIER_POINTER(DECL_NAME(inner));
                            //fprintf(stderr, "\tfound name %s\n", name);
                            if (strcmp(name, "lock") == 0) {
                                fprintf(stderr, "\tfound lock!\n");
                                fprintf(stderr, "lock statement %p\n", stmt);
                                auto lhs = gimple_call_lhs(stmt);
                                if (lhs != nullptr) {
                                    //fprintf(stderr, "\t\tlhs %d %d\n", TREE_CODE(lhs), TREE_CODE_CLASS(TREE_CODE(lhs)));
                                    //fprintf(stderr, "\t\t%d %d %d\n", IDENTIFIER_NODE, SSA_NAME, PLACEHOLDER_EXPR);
                                }
                                if (gimple_call_num_args(stmt) > 0) {
                                    auto rhs = gimple_call_arg(stmt, 0);
                                    auto real_rhs = follow_ssa(rhs);
                                    if (real_rhs != nullptr) {
                                        fprintf(stderr, "\t\trhs %d %p\n", TREE_CODE(real_rhs), real_rhs);
                                        if (TREE_CODE(real_rhs) == VAR_DECL) {
                                            fprintf(stderr, "\t\trhs decl %s\n", IDENTIFIER_POINTER(DECL_NAME(real_rhs)));
                                        }
                                        //fprintf(stderr, "\t\t%d %d %d\n", IDENTIFIER_NODE, SSA_NAME, PLACEHOLDER_EXPR);

                                    }
                                        //fprintf(stderr, "\t\t%p\n", rhs);
                                } else {
                                        //fprintf(stderr, "\t\tno args\n");
                                }

                            }
                        }
                        // TODO
                    }
                }
                if (false && gs->code == GIMPLE_ASSIGN) {
                    auto* stmt = as_a<gassign*>(gs);
                    tree lhs = gimple_assign_lhs(stmt);
                    tree rhs = gimple_assign_rhs1(stmt);
                    tree rhs2 = gimple_assign_rhs2(stmt);
                    auto subcode = gimple_assign_rhs_code(gs);
                    fprintf(stderr, "found statement subcode %d lhs %d rhs %d\n", subcode, TREE_CODE(lhs), TREE_CODE(rhs));
                    fprintf(stderr, "tree codes ssa_name %d var_decl %d int_cst %d\n", SSA_NAME, VAR_DECL, INTEGER_CST);
                    if (TREE_CODE(lhs) == SSA_NAME) {
                        fprintf(stderr, "\t\tlhs points to %p\n", SSA_NAME_DEF_STMT(lhs));
                    }
                    if (TREE_CODE(rhs) == SSA_NAME) {
                        fprintf(stderr, "\t\trhs points to %p\n", SSA_NAME_DEF_STMT(rhs));
                    }
                    if (subcode == INTEGER_CST) {
                        fprintf(stderr, "\t\trhs %p is constant %llx\n", SSA_NAME_DEF_STMT(rhs), (long long int)tree_to_shwi(rhs));
                    }
                    if (subcode == VAR_DECL) {
                        fprintf(stderr, "var decl %p\n", DECL_NAME(rhs));

                        /*
                        pretty_printer pp;
                        pp_needs_newline(&pp) = true;
                        pp.set_output_stream(stderr);
                        print_declaration(&pp, rhs, 3, (dump_flags_t)0);
                        pp_newline_and_flush(&pp);
                        */
                    }

                    if (rhs2 != NULL && TREE_CODE(rhs2) == SSA_NAME) {
                        fprintf(stderr, "\t\trhs2 points to %p\n", SSA_NAME_DEF_STMT(rhs2));
                    }
                }
                /*
                if (gs->code == GIMPLE_GOTO) {
                    fprintf(stderr, "\tfound goto\n");
                }
                */
                /*
                if (gs->code == GIMPLE_RETURN) {
                    fprintf(stderr, "\tfound return\n");
                    auto* stmt = as_a<greturn*>(gs);
                    if (stmt != NULL) {
                        auto retval = gimple_return_retval(stmt);
                        if (retval != NULL) {
                            if (TREE_CODE(retval) == SSA_NAME) {
                                //fprintf(stderr, "\t\tret points to %p\n", SSA_NAME_DEF_STMT(retval));
                                pp_needs_newline(&pp) = true;
                                pp.set_output_stream(stderr);
                                pp_gimple_stmt_1(&pp, SSA_NAME_DEF_STMT(retval), 0, TDF_RAW);
                                pp_newline_and_flush(&pp);
                            }
                        }
                    } else {
                        fprintf(stderr, "UNEXPECTED; GIMPLE_RETURN but not greturn\n");
                    }
                }
                */
                if (gs->code == GIMPLE_COND) {
                    fprintf(stderr, "\tfound cond\n");
                    gcond* stmt = as_a<gcond*>(gs);
                    /*
                    pretty_printer pp;
                    pp_needs_newline(&pp) = true;
                    pp.set_output_stream(stderr);
                    pp_gimple_stmt_1(&pp, stmt, 0, TDF_RAW);
                    pp_newline_and_flush(&pp);
                    */

                    tree lhs = gimple_cond_lhs(stmt);
                    tree rhs = gimple_cond_rhs(stmt);
                    // labels are gone; a pass removed then
                    // the data is now encoded in the cfg edges
                    fprintf(stderr, "\t\tlhs: ");
                    print_generic_expr(stderr, lhs);
                    fprintf(stderr, " trhs: ");
                    print_generic_expr(stderr, rhs);
                    fprintf(stderr, "\n");

                }
                if (gs->code == GIMPLE_LABEL) {
                    fprintf(stderr, "\tfound label\n");
                }

                c++;
            }
            edge e;
            edge_iterator ei;
            FOR_EACH_EDGE(e, ei, bb->succs) {
                basic_block dest = e->dest;
                fprintf(stderr, "-> %d %04x\n", dest->index, e->flags);
            }

    }

        /*
        gimple_stmt_iterator gsi;
        for (gsi = gsi_start(f->gimple_body); !gsi_end_p(gsi); gsi_next(&gsi)) {
            gimple* g = gsi_stmt(gsi);
            fprintf(stderr, "test\n");
            print_gimple_stmt(stderr, g, 0);
        }
        */
        return 0;
    }
};

// Plugin initialization function
int plugin_init(plugin_name_args *plugin_info, plugin_gcc_version *version) {
    // Check for GCC version compatibility
    if (!plugin_default_version_check(version, &gcc_version)) {
        return 1; // Incompatible version
    }

    struct register_pass_info pass_info = {
        .pass = new my_custom_pass(g),
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
