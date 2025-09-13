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
    using lock_state_type = decltype(edge_state<T, int>{}.cur_lock_state);

    typename T::Location loc;
    lock_state_type cur_lock_state;
    typename T::FuncId caller;

    bool operator==(const callsite<T>& other) const {
        return (loc == other.loc) && (cur_lock_state == other.cur_lock_state) && (caller == other.caller);
    }
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

        std::vector<std::pair<callsite<T>, FuncId>> calls;
        lock_state<lock> blocking_locks = {};

        {
            for (const auto& lock_id: fun.locks) {
                // if we haven't seen this lock before, add it to the global list
                if (auto it = lock_idx.find(lock_id); it == lock_idx.end()) {
                    lock_idx[lock_id] = {(int)locks.size()};
                    locks.push_back(lock_id);

                    if (locks.size() > 32) {
                        // TODO error out
                    }
                }
            }
        }


        fun.template explore<int>([&](edge_state<T, int>& es, const bb<T>& basic_block, const action<T>& a) {
            if (a.typ == kLock) {
                blocking_locks = blocking_locks | a.lock_id->mask();

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
                if (auto it = functions.find(*a.called_func); it != functions.end()) {
                    auto translated = to_global(es.cur_lock_state, name);
                    if ((translated & blocking_locks_used[*a.called_func]) != 0) {
                        // TODO add an error
                        line_errors[a.loc].add(errors::call_with_blocking_lock(""));
                    }
                } else {
                    if (es.cur_lock_state != 0) {
                        calls.push_back({callsite<T>{a.loc, es.cur_lock_state, name}, *a.called_func});

                        // TODO: deal with transitive dependencies; 
                    }
                }
            } else if (a.typ == kEnd) {
                if (es.cur_lock_state != 0) {
                    // lock held at the end of the function
                    line_errors[a.loc].add(errors::take_without_give(""));
                }
            }
        }, 0);

        blocking_locks_used[name] = to_global(blocking_locks, name);

        for (auto &call: calls) {
            auto [callsite, func_handle] = call;
            called_by[func_handle].push_back(callsite);
        }


        if (auto it = called_by.find(name); it != called_by.end()) {
            auto &calls = it->second;
            for (const auto& cs: calls) {
                if ((to_global(cs.cur_lock_state, name) & blocking_locks_used[cs.caller]) != 0) {
                    // TODO: add an error
                    line_errors[cs.loc].add(errors::call_with_blocking_lock(""));
                }
            }
            //called_by.erase(it);

            // TODO propagate up call chain; update the blocking locks used by
            // all the callers
        }
    }
};

}

template <typename T, typename U> struct std::hash<std::pair<lock_checker::callsite<T>, U>> {
    std::size_t operator()(const std::pair<lock_checker::callsite<T>, U>& c) const {
        std::size_t a1 = std::hash<typename T::Location>{}(c.first.loc);
        std::size_t a2 = std::hash<typename lock_checker::callsite<T>::lock_state_type>{}(c.first.cur_lock_state);
        std::size_t a3 = std::hash<typename T::FuncId>{}(c.first.fh);
        std::size_t a4 = std::hash<U>{}(c.second);

        return a1 ^ (a2 << 1) ^ (a3 << 2) ^ (a4 << 3);
    }
};
