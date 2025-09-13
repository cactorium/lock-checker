#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "file_checker.hh"

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

namespace lock_checker {
struct BasicAdapter {
    using FuncId = std::string;
    using Location = int;
    using LockId = int;
};

TEST(test_file_checker, test_basic) {
    using a = action<BasicAdapter>;
    using ix = idx<lock>;

    auto foo = lock_checker::func<BasicAdapter> {
        // foo() {
        //      lock(portMAX_DELAY); // 1
        //      if (a) {
        //          unlock(a); // 2
        //      } else {
        //          f(); // 3
        //          unlock(a);
        //      }
        //      lock(portMAX_DELAY); // 4
        //      unlock();
        // }
        .locks = { 0 },
        .bbs = {
            { // 0
                .actions = {},
                .next = {
                    {1},
                }
            },
            { // 1
                .actions = {
                    a::lock_(0, ix{0}),
                },
                .next = {
                    {2}, {3}
                }
            },
            { // 2
                .actions = {
                    a::unlock_(2, ix{0}),
                },
                .next = {
                    {4}
                }
            },
            { // 3
                .actions = {
                    a::call_(3, std::string("f")),
                    a::unlock_(4, ix{0}),
                },
                .next = {
                    {4}
                }
            },
            { // 4
                .actions = {
                    a::lock_(5, ix{0}),
                    a::unlock_(6, ix{0}),
                },
                .next = {
                    {5}
                }
            },
            { // 5
            },
        },
        .start_bb = {0},
        .end_bb = {5},
    };

    file_checker<BasicAdapter> fc;

    std::unordered_map<int, errors> line_errors;

    fc.process_function("foo", std::move(foo), line_errors);
    ASSERT_EQ(line_errors.size(), 0);
}

TEST(test_file_checker, test_basic_blocking_call) {
    using a = action<BasicAdapter>;
    using ix = idx<lock>;

    auto foo = lock_checker::func<BasicAdapter> {
        // foo() {
        //      lock(portMAX_DELAY); // 1
        //      if (a) {
        //          unlock(a); // 2
        //      } else {
        //          f(); // 3
        //          unlock(a);
        //      }
        //      lock(portMAX_DELAY); // 4
        //      unlock();
        // }
        .locks = { 0 },
        .bbs = {
            { // 0
                .actions = {},
                .next = {
                    {1},
                }
            },
            { // 1
                .actions = {
                    a::lock_(0, ix{0}),
                },
                .next = {
                    {2}, {3}
                }
            },
            { // 2
                .actions = {
                    a::unlock_(2, ix{0}),
                },
                .next = {
                    {4}
                }
            },
            { // 3
                .actions = {
                    a::call_(3, std::string("f")),
                    a::unlock_(4, ix{0}),
                },
                .next = {
                    {4}
                }
            },
            { // 4
                .actions = {
                    a::lock_(5, ix{0}),
                    a::unlock_(6, ix{0}),
                },
                .next = {
                    {5}
                }
            },
            { // 5
            },
        },
        .start_bb = {0},
        .end_bb = {5},
    };

    // f() {
    //      lock(portMAX_DELAY);
    //      unlock();
    // }
    auto f = lock_checker::func<BasicAdapter> {
        .locks = { 0 },
        .bbs = {
            { // 0
                .actions = {},
                .next = {
                    {1},
                }
            },
            { // 1
                .actions = {
                    a::lock_(1, ix{0}),
                    a::unlock_(1, ix{0}),
                },
                .next = {
                    {2},
                }
            },
            { // 2
            },
        },
        .start_bb = {0},
        .end_bb = {2},
    };

    file_checker<BasicAdapter> fc;

    std::unordered_map<int, errors> line_errors;

    fc.process_function("f", std::move(f), line_errors);
    ASSERT_EQ(line_errors.size(), 0);
    fc.process_function("foo", std::move(foo), line_errors);
    ASSERT_NE(line_errors.size(), 0);
}

TEST(test_file_checker, test_basic_blocking_call2) {
    using a = action<BasicAdapter>;
    using ix = idx<lock>;

    auto foo = lock_checker::func<BasicAdapter> {
        // foo() {
        //      lock(portMAX_DELAY); // 1
        //      if (a) {
        //          unlock(a); // 2
        //      } else {
        //          f(); // 3
        //          unlock(a);
        //      }
        //      lock(portMAX_DELAY); // 4
        //      unlock();
        // }
        .locks = { 0 },
        .bbs = {
            { // 0
                .actions = {},
                .next = {
                    {1},
                }
            },
            { // 1
                .actions = {
                    a::lock_(0, ix{0}),
                },
                .next = {
                    {2}, {3}
                }
            },
            { // 2
                .actions = {
                    a::unlock_(2, ix{0}),
                },
                .next = {
                    {4}
                }
            },
            { // 3
                .actions = {
                    a::call_(3, std::string("f")),
                    a::unlock_(4, ix{0}),
                },
                .next = {
                    {4}
                }
            },
            { // 4
                .actions = {
                    a::lock_(5, ix{0}),
                    a::unlock_(6, ix{0}),
                },
                .next = {
                    {5}
                }
            },
            { // 5
            },
        },
        .start_bb = {0},
        .end_bb = {5},
    };

    // f() {
    //      lock(portMAX_DELAY);
    //      unlock();
    // }
    auto f = lock_checker::func<BasicAdapter> {
        .locks = { 0 },
        .bbs = {
            { // 0
                .actions = {},
                .next = {
                    {1},
                }
            },
            { // 1
                .actions = {
                    a::lock_(1, ix{0}),
                    a::unlock_(1, ix{0}),
                },
                .next = {
                    {2},
                }
            },
            { // 2
            },
        },
        .start_bb = {0},
        .end_bb = {2},
    };

    file_checker<BasicAdapter> fc;

    std::unordered_map<int, errors> line_errors;

    // swap the order around
    fc.process_function("foo", std::move(foo), line_errors);
    ASSERT_EQ(line_errors.size(), 0);
    fc.process_function("f", std::move(f), line_errors);
    ASSERT_NE(line_errors.size(), 0);
}


TEST(test_file_checker, test_missing_give) {
    using a = action<BasicAdapter>;
    using ix = idx<lock>;

    auto foo = lock_checker::func<BasicAdapter> {
        // foo() {
        //      lock(portMAX_DELAY); // 1
        //      if (a) {
        //          unlock(a); // 2
        //      } else {
        //          f(); // 3
        //          unlock(a);
        //      }
        //      lock(portMAX_DELAY); // 4
        //      unlock();
        // }
        .locks = { 0 },
        .bbs = {
            { // 0
                .actions = {},
                .next = {
                    {1},
                }
            },
            { // 1
                .actions = {
                    a::lock_(0, ix{0}),
                },
                .next = {
                    {2}, {3}
                }
            },
            { // 2
                .actions = {
                    a::unlock_(2, ix{0}),
                },
                .next = {
                    {4}
                }
            },
            { // 3
                .actions = {
                    a::call_(3, std::string("f")),
                    //a::unlock_(4, ix{0}),
                },
                .next = {
                    {4}
                }
            },
            { // 4
                .actions = {
                    a::lock_(5, ix{0}),
                    a::unlock_(6, ix{0}),
                },
                .next = {
                    {5}
                }
            },
            { // 5
            },
        },
        .start_bb = {0},
        .end_bb = {5},
    };

    file_checker<BasicAdapter> fc;

    std::unordered_map<int, errors> line_errors;

    // swap the order around
    fc.process_function("foo", std::move(foo), line_errors);
    ASSERT_NE(line_errors.size(), 0);
}

TEST(test_file_checker, test_missing_give_return) {
    using a = action<BasicAdapter>;
    using ix = idx<lock>;

    auto foo = lock_checker::func<BasicAdapter> {
        // foo() {
        //      lock(portMAX_DELAY); // 1
        //      if (a) {
        //          unlock(a); // 2
        //      } else {
        //          f(); // 3
        //          unlock(a);
        //      }
        //      lock(portMAX_DELAY); // 4
        //      //unlock();
        // }
        .locks = { 0 },
        .bbs = {
            { // 0
                .actions = {},
                .next = {
                    {1},
                }
            },
            { // 1
                .actions = {
                    a::lock_(0, ix{0}),
                },
                .next = {
                    {2}, {3}
                }
            },
            { // 2
                .actions = {
                    a::unlock_(2, ix{0}),
                },
                .next = {
                    {4}
                }
            },
            { // 3
                .actions = {
                    a::call_(3, std::string("f")),
                    a::unlock_(4, ix{0}),
                },
                .next = {
                    {4}
                }
            },
            { // 4
                .actions = {
                    a::lock_(5, ix{0}),
                    //a::unlock_(6, ix{0}),
                },
                .next = {
                    {5}
                }
            },
            { // 5
            },
        },
        .start_bb = {0},
        .end_bb = {5},
    };

    file_checker<BasicAdapter> fc;

    std::unordered_map<int, errors> line_errors;

    // swap the order around
    fc.process_function("foo", std::move(foo), line_errors);
    ASSERT_NE(line_errors.size(), 0);
}


TEST(test_file_checker, test_missing_take) {
    using a = action<BasicAdapter>;
    using ix = idx<lock>;

    auto foo = lock_checker::func<BasicAdapter> {
        // foo() {
        //      //lock(portMAX_DELAY); // 1
        //      if (a) {
        //          unlock(a); // 2
        //      } else {
        //          f(); // 3
        //          unlock(a);
        //      }
        //      lock(portMAX_DELAY); // 4
        //      unlock();
        // }
        .locks = { 0 },
        .bbs = {
            { // 0
                .actions = {},
                .next = {
                    {1},
                }
            },
            { // 1
                .actions = {
                    //a::lock_(0, ix{0}),
                },
                .next = {
                    {2}, {3}
                }
            },
            { // 2
                .actions = {
                    a::unlock_(2, ix{0}),
                },
                .next = {
                    {4}
                }
            },
            { // 3
                .actions = {
                    a::call_(3, std::string("f")),
                    a::unlock_(4, ix{0}),
                },
                .next = {
                    {4}
                }
            },
            { // 4
                .actions = {
                    a::lock_(5, ix{0}),
                    //a::unlock_(6, ix{0}),
                },
                .next = {
                    {5}
                }
            },
            { // 5
            },
        },
        .start_bb = {0},
        .end_bb = {5},
    };

    file_checker<BasicAdapter> fc;

    std::unordered_map<int, errors> line_errors;

    fc.process_function("foo", std::move(foo), line_errors);
    ASSERT_NE(line_errors.size(), 0);
}


TEST(test_file_checker, test_multiple_funcs) {
    using a = action<BasicAdapter>;
    using ix = idx<lock>;

    auto foo = lock_checker::func<BasicAdapter> {
        // foo() {
        //      lock(portMAX_DELAY); // 1
        //      bar();
        //      unlock();
        // }
        .locks = { 0 },
        .bbs = {
            { // 0
                .actions = {},
                .next = {
                    {1},
                }
            },
            { // 1
                .actions = {
                    a::lock_(0, ix{0}),
                    a::call_(1, "bar"),
                    a::unlock_(2, ix{0}),
                },
                .next = {
                    {2}
                }
            },
            { // 2
            },
        },
        .start_bb = {0},
        .end_bb = {2},
    };
    auto bar = lock_checker::func<BasicAdapter> {
        // bar() {
        //      baz();
        // }
        .locks = { 0 },
        .bbs = {
            { // 0
                .actions = {},
                .next = {
                    {1},
                }
            },
            { // 1
                .actions = {
                    a::call_(3, "baz"),
                },
                .next = {
                    {2}
                }
            },
            { // 2
            },
        },
        .start_bb = {0},
        .end_bb = {2},
    };
    auto baz = lock_checker::func<BasicAdapter> {
        // baz() {
        //      lock();
        //      unlock();
        // }
        .locks = { 0 },
        .bbs = {
            { // 0
                .actions = {},
                .next = {
                    {1},
                }
            },
            { // 1
                .actions = {
                    a::lock_(4, ix{0}),
                    a::unlock_(5, ix{0}),
                },
                .next = {
                    {2}
                }
            },
            { // 2
            },
        },
        .start_bb = {0},
        .end_bb = {2},
    };


    {
        file_checker<BasicAdapter> fc;
        std::unordered_map<int, errors> line_errors;

        fc.process_function("foo", foo, line_errors);
        ASSERT_EQ(line_errors.size(), 0);
        fc.process_function("bar", bar, line_errors);
        ASSERT_EQ(line_errors.size(), 0);
        fc.process_function("baz", baz, line_errors);
        ASSERT_NE(line_errors.size(), 0);
    }

    // test out different orders
    {
        file_checker<BasicAdapter> fc;
        std::unordered_map<int, errors> line_errors;

        fc.process_function("baz", baz, line_errors);
        ASSERT_EQ(line_errors.size(), 0);
        fc.process_function("foo", foo, line_errors);
        ASSERT_EQ(line_errors.size(), 0);
        fc.process_function("bar", bar, line_errors);
        ASSERT_NE(line_errors.size(), 0);
    }

    {
        file_checker<BasicAdapter> fc;
        std::unordered_map<int, errors> line_errors;

        fc.process_function("baz", baz, line_errors);
        ASSERT_EQ(line_errors.size(), 0);
        fc.process_function("bar", bar, line_errors);
        ASSERT_EQ(line_errors.size(), 0);
        fc.process_function("foo", foo, line_errors);
        ASSERT_NE(line_errors.size(), 0);
    }



}


}
