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
    using FuncHandler = std::string;
    using Location = int;
};

TEST(test_file_checker, test_basic) {
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
        .bbs = {
            { // 0
                .actions = {},
                .loc = 0,
                .next = {
                    {1},
                }
            },
            { // 1
                .actions = {
                    {kLock, 1},
                },
                .loc=0,
                .next = {
                    {2}, {3}
                }
            },
            { // 2
                .actions = {
                    {kUnlock, 2},
                },
                .loc=1,
                .next = {
                    {4}
                }
            },
            { // 3
                .actions = {
                    {kCall, 3, std::nullopt, std::string("f")},
                    {kUnlock, 4},
                },
                .loc=3,
                .next = {
                    {4}
                }
            },
            { // 4
                .actions = {
                    {kLock, 5},
                    {kUnlock, 6},
                },
                .loc=5,
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

}
