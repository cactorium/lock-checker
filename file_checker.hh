#pragma once

#include <unordered_map>
#include <unordered_set>

#include "func_walker.hh"

namespace lock_checker {

struct errors {

    void merge(const errors& other) {
        // TODO
    }
};

template <typename T> struct callsite {
    typename T::Location loc;
    using lock_state_type = decltype(edge_state<T, int>{}.locked_by_task);
    lock_state_type lock_state;

    bool operator==(const callsite<T>& other) const {
        return (loc == other.loc) && (lock_state == other.lock_state);
    }
};

template <typename T> struct file_checker {
    using FHandle = typename T::FuncHandler;
    using Location = typename T::Location;

    std::unordered_map<FHandle, func<T>> functions;
    std::unordered_map<FHandle, bool> deadlock_with_lock; // if the function will deadlock if the lock is taken before it's called
    std::unordered_map<FHandle, std::vector<callsite<T>>> called_by;

    void process_function(FHandle name, func<T> && f, std::unordered_map<Location, errors>& line_errors) {

        functions[name] = f;

        func<T>& fun = functions[name];
        bool uses_blocking_lock = false;
        std::unordered_set<callsite<T>> unique_calls;

        fun.template explore<int>([&](edge_state<T, int>& es, const bb<T>& basic_block, const action<T>& a) {
            if (a.typ == kLock) {
                uses_blocking_lock = true;
                if (es.locked_by_task) {
                    // TODO add an error; double lock
                }
            } else if (a.typ == kUnlock) {
                if (!es.locked_by_task) {
                    // TODO add an error; unlock with a lock
                }
            } else if (a.typ == kCall) {
                if (a.called_func.has_value()) {
                    if (auto it = functions.find(*a.called_func); it != functions.end()) {
                        if (es.locked_by_task && deadlock_with_lock[*a.called_func]) {
                            // TODO add an error
                        }
                    } else {
                        if (es.locked_by_task) {
                            unique_calls.insert(callsite<T>{a.loc, es.locked_by_task});
                        }
                    }
                }
            } else if (a.typ == kEnd) {
                if (es.locked_by_task) {
                    // TODO add an error; lock held at the end of the function
                }
            }
        }, 0);

        deadlock_with_lock[name] = uses_blocking_lock;

        if (auto it = called_by.find(name); it != called_by.end()) {
            auto &calls = it->second;
            for (const auto& cs: calls) {
                if (uses_blocking_lock && cs.lock_state) {
                    // TODO: add an error
                }
            }
            called_by.erase(it);
        }
    }
};

}

template <typename T> struct std::hash<lock_checker::callsite<T>> {
    std::size_t operator()(const lock_checker::callsite<T>& c) const {
        std::size_t a = std::hash<typename T::Location>{}(c.loc);
        std::size_t b = std::hash<typename lock_checker::callsite<T>::lock_state_type>{}(c.lock_state);

        return (a << 1) ^ b;
    }
};
