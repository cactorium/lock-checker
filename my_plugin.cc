#include "gcc-plugin.h"
#include "plugin-version.h"

#include "context.h"
#include "diagnostic.h"
#include "plugin.h"
#include "tree.h"
#include "tree-pass.h"

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
    .properties_provided = PROP_cfg,
};

struct my_custom_pass: public gimple_opt_pass {
public:
    my_custom_pass(gcc::context* ctx): gimple_opt_pass(my_pass_data, ctx) {}

    virtual unsigned int execute(function* f) override {
        warning(0, "in function %s", IDENTIFIER_POINTER(DECL_NAME(f->decl)));
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
        .reference_pass_name = "ssa",
        .ref_pass_instance_number = 1,
        .pos_op = PASS_POS_INSERT_BEFORE,
    };

    // Register the callback
    // Here, we register it for the 'PLUGIN_START_UNIT' event,
    // which occurs at the beginning of compiling a translation unit.
    register_callback(plugin_info->base_name, PLUGIN_START_UNIT, my_callback, NULL);
    register_callback(plugin_info->base_name, PLUGIN_PASS_MANAGER_SETUP, NULL, &pass_info);

    fprintf(stderr, "GCC Plugin: My plugin loaded successfully!\n");
    return 0; // Success
}
