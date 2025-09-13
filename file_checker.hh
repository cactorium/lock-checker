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

template <typename T> struct file_checker;

template <typename T> struct callsite {
    typename T::Location loc;
    lock_state<file_checker<T>> cur_lock_state;
    typename T::FuncId caller;
};

template <typename T> struct file_checker {
    using FuncId = typename T::FuncId;
    using Location = typename T::Location;
    using LockId = typename T::LockId;

    // global list of lock ids and their position in 
    // lock_state<file_checker<T>> objects
    std::unordered_map<LockId, idx<file_checker<T>>> lock_idx;
    std::vector<LockId> locks;

    std::unordered_map<FuncId, func<T>> functions; // might not need this
    std::unordered_map<FuncId, lock_state<file_checker<T>>> blocking_locks_used; // bitfield of all the locks that are taken using a blocking call in the function
    std::unordered_map<FuncId, std::vector<callsite<T>>> called_by;

    lock_state<file_checker<T>> to_global(const lock_state<lock>& caller_state, FuncId caller) const {
        const auto &func_locks = functions.find(caller)->second.locks;
        lock_state<file_checker<T>> caller_state_translated = { 0 };

        for (int i = 0; i < func_locks.size(); i++) {
            if ((caller_state & (1 << i)) != 0) {
                const auto lock_id = func_locks[i];
                caller_state_translated = caller_state_translated | lock_idx.find(lock_id)->second.mask();
            }
        }

        return caller_state_translated;
    }

    void check_callers(FuncId callee, std::unordered_map<Location, errors>& line_errors) {
        const auto &called_by_ = called_by;
        if (const auto it = called_by_.find(callee); it != called_by.end()) {
            for (const auto& cs: it->second) {
                // check this call to see if it causes an error
                if ((blocking_locks_used[callee] & cs.cur_lock_state) != 0) {
                    line_errors[cs.loc].add(errors::call_with_blocking_lock(""));
                }

                // update blocking_locks_used
                const auto old_locks = blocking_locks_used[cs.caller];
                blocking_locks_used[cs.caller] = blocking_locks_used[cs.caller] | blocking_locks_used[callee];

                // if blocking_locks_used is changed, check the functions that called it
                if (old_locks != blocking_locks_used[cs.caller]) {
                    check_callers(cs.caller, line_errors);
                }

                // this is guaranteed to terminate because it only recurses if block_locks_used is updated
                // which can only happen up to 32 times
            }
        }
    }

    // TODO memoize?
    void process_function(FuncId name, func<T> && f, std::unordered_map<Location, errors>& line_errors) {
        functions[name] = f;
        process_function_internal(name, line_errors);
    }
    void process_function(FuncId name, const func<T> & f, std::unordered_map<Location, errors>& line_errors) {
        functions[name] = f;
        process_function_internal(name, line_errors);
    }

    void process_function_internal(FuncId name, std::unordered_map<Location, errors>& line_errors) {
        const auto& fun = functions[name];

        lock_state<file_checker<T>> blocking_locks = {};

        {
            // add any locks the global list is missing
            for (const auto& lock_id: fun.locks) {
                if (locks.size() > 32) {
                    // TODO error out
                }
                if (auto it = lock_idx.find(lock_id); it == lock_idx.end()) {
                    lock_idx[lock_id] = {(int)locks.size()};
                    locks.push_back(lock_id);
                }
            }
        }


        fun.template explore<int>([&](edge_state<T, int>& es, const bb<T>& basic_block, const action<T>& a) {
            if (a.typ == kLock) {
                blocking_locks = blocking_locks | to_global(a.lock_id->mask(), name);

                auto lock_mask = a.lock_id->mask();
                if ((es.cur_lock_state & lock_mask) != 0) {
                    // double lock
                    line_errors[a.loc].add(errors::double_lock(""));
                }
            } else if (a.typ == kUnlock) {
                auto lock_mask = a.lock_id->mask();
                if ((es.cur_lock_state & lock_mask) == 0) {
                    // unlock without a lock
                    line_errors[a.loc].add(errors::give_without_take(""));
                }
            } else if (a.typ == kCall) {
                if (auto it = blocking_locks_used.find(*a.called_func); it != blocking_locks_used.end()) {
                    auto translated = to_global(es.cur_lock_state, name);
                    if ((translated & it->second) != 0) {
                        // TODO add an error
                        line_errors[a.loc].add(errors::call_with_blocking_lock(""));
                    }
                    blocking_locks = blocking_locks | it->second;
                }
                // add to call graph
                called_by[*a.called_func].push_back(callsite<T>{a.loc, to_global(es.cur_lock_state, name), name});
            } else if (a.typ == kEnd) {
                if (es.cur_lock_state != 0) {
                    // lock held at the end of the function
                    line_errors[a.loc].add(errors::take_without_give(""));
                }
            }
        }, 0);

        blocking_locks_used[name] = blocking_locks;

        check_callers(name, line_errors);
    }
};

}
