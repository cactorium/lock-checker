#pragma once

#include <unordered_map>
#include <unordered_set>
#include <string>

#include "func_walker.hh"

namespace lock_checker {

struct error {
    enum {
        kDoubleTake, // two takes in a row
        kGiveWithoutTake, // giving without taking first
        kTakeWithoutGive, // missing give before return
        kCallWithBlockingLock // a take followed by a blocking take in a function call
    } typ;

    std::string extra;
};

struct errors {
    std::vector<error> errs;

    static error double_lock(const std::string& extra) {
        return error {
            error::kDoubleTake,
            extra
        };
    }
    static error give_without_take(const std::string& extra) {
        return error {
            error::kGiveWithoutTake,
            extra
        };
    }
    static error take_without_give(const std::string& extra) {
        return error {
            error::kTakeWithoutGive,
            extra
        };
    }
    static error call_with_blocking_lock(const std::string& extra) {
        return error {
            error::kCallWithBlockingLock,
            extra
        };
    }

    void add(const error& err) {
        errs.push_back(err);
    }
    void add(error&& err) {
        errs.push_back(err);
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
        process_function_internal(name, line_errors);
    }
    void process_function(FHandle name, const func<T> & f, std::unordered_map<Location, errors>& line_errors) {
        functions[name] = f;
        process_function_internal(name, line_errors);
    }

    void process_function_internal(FHandle name, std::unordered_map<Location, errors>& line_errors) {
        func<T>& fun = functions[name];
        bool uses_blocking_lock = false;
        std::unordered_set<std::pair<callsite<T>, FHandle>> unique_calls;

        fun.template explore<int>([&](edge_state<T, int>& es, const bb<T>& basic_block, const action<T>& a) {
            if (a.typ == kLock) {
                uses_blocking_lock = true;
                if (es.locked_by_task) {
                    // double lock
                    line_errors[a.loc].add(errors::double_lock(""));
                }
            } else if (a.typ == kUnlock) {
                if (!es.locked_by_task) {
                    // unlock with a lock
                    line_errors[a.loc].add(errors::give_without_take(""));
                }
            } else if (a.typ == kCall) {
                if (a.called_func.has_value()) {
                    if (auto it = functions.find(*a.called_func); it != functions.end()) {
                        if (es.locked_by_task && deadlock_with_lock[*a.called_func]) {
                            // TODO add an error
                            line_errors[a.loc].add(errors::call_with_blocking_lock(""));
                        }
                    } else {
                        if (es.locked_by_task) {
                            unique_calls.insert({callsite<T>{a.loc, es.locked_by_task}, *a.called_func});
                        }
                    }
                }
            } else if (a.typ == kEnd) {
                if (es.locked_by_task) {
                    // lock held at the end of the function
                    line_errors[a.loc].add(errors::take_without_give(""));
                }
            }
        }, 0);

        deadlock_with_lock[name] = uses_blocking_lock;
        for (auto &call: unique_calls) {
            auto [callsite, func_handle] = call;
            called_by[func_handle].push_back(callsite);
        }


        if (auto it = called_by.find(name); it != called_by.end()) {
            auto &calls = it->second;
            for (const auto& cs: calls) {
                if (uses_blocking_lock && cs.lock_state) {
                    // TODO: add an error
                    line_errors[cs.loc].add(errors::call_with_blocking_lock(""));
                }
            }
            called_by.erase(it);
        }
    }
};

}

template <typename T, typename U> struct std::hash<std::pair<lock_checker::callsite<T>, U>> {
    std::size_t operator()(const std::pair<lock_checker::callsite<T>, U>& c) const {
        std::size_t a1 = std::hash<typename T::Location>{}(c.first.loc);
        std::size_t a2 = std::hash<typename lock_checker::callsite<T>::lock_state_type>{}(c.first.lock_state);
        std::size_t a3 = std::hash<U>{}(c.second);

        return a1 ^ (a2 << 1) ^ (a3 << 1);
    }
};
