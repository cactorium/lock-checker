#include "gcc-plugin.h"
#include "plugin.h"
#include "plugin-version.h"

#include "context.h"
#include "diagnostic.h"
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

struct my_custom_pass: public gimple_opt_pass {
public:
    my_custom_pass(gcc::context* ctx): gimple_opt_pass(my_pass_data, ctx) {}

    virtual unsigned int execute(function* f) override {
        warning(0, "in function %s", IDENTIFIER_POINTER(DECL_NAME(f->decl)));

        basic_block bb;
        int c = 0;
        FOR_ALL_BB_FN(bb, f) {
            gimple_bb_info* bb_info = &bb->il.gimple;
            fprintf(stderr, "bb start %d\n", bb->index);
            //print_gimple_seq(stderr, bb_info->seq, 0, (dump_flags_t)0);
            gimple_stmt_iterator gsi;
            for (gsi = gsi_start(bb_info->seq); !gsi_end_p(gsi); gsi_next(&gsi)) {
                gimple* gs = gsi_stmt(gsi);
                fprintf(stderr, "\tstmt %d code %d\n\t", c, gs->code);
                print_gimple_stmt(stderr, gs, TDF_RAW);
                if (gs->code == GIMPLE_GOTO) {
                    fprintf(stderr, "\tfound goto\n");
                }
                if (gs->code == GIMPLE_RETURN) {
                    fprintf(stderr, "\tfound return\n");
                }
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
