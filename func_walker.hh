#pragma once

#include <cstdint>

#include <optional>
#include <queue>
#include <unordered_set>
#include <vector>

// almost everything here is templated around a generic variable T, which needs to provide two types,
// - a Location type for mapping a lock, unlock, or call to its location in code,
// - and a FuncHandler type that provides a unique identifier for the function
//
// this is genericized so that unit testing can be done on this code without needing to use the representations
// GCC provides; we can mock out gcc's tree and location_t for our own structs in testing

namespace lock_checker {

template <typename T> struct idx {
    int idx_;

    idx(): idx_(-1) {}
    idx(int v): idx_(v) {}
    int& operator*() {
        return idx_;
    }
    const int& operator*() const {
        return idx_;
    }
    
    bool operator==(const idx<T>& other) const {
        return idx_ == *other;
    }
};

struct fallible_lock;
template <typename T> struct bb;

enum action_type {
    kLock,
    kFallibleLock,
    kUnlock,
    kCall,
    kEnd // end of function reached
};
template <typename T> struct action {
    action_type typ;
    typename T::Location loc; // location in code to notify in case of error
    std::optional<idx<fallible_lock>> lock_id; // only used for fallible locks
    std::optional<typename T::FuncHandler> called_func; // pointer to the function declaration for calls
};

template <typename T> struct cond_edge {
    idx<bb<T>> on_true;
    std::optional<idx<bb<T>>> on_false;
    std::optional<idx<fallible_lock>> depends_on; // index of the fallible lock call this depends on, else use the true edge

    //cond_edge(): on_true() {}
};

template <typename T> struct bb {
    std::vector<action<T>> actions; // actions to do to replay a basic_block
    typename T::Location loc; // where to alert if a semaphore isn't unlocked properly
    cond_edge<T> next;

    //bb(): end(), next() {}
};

template <typename T, typename U> struct edge_state {
    uint32_t fallible_locks; // state of fallible lock calls up to this point
    idx<bb<T>> bb_idx;
    bool locked_by_task;
    U added; // any extra info that the caller wants to add to the state

    bool operator==(const edge_state<T, U>& other) const {
        return (other.fallible_locks == fallible_locks) && (*other.bb_idx == *bb_idx) && (other.locked_by_task == locked_by_task);
    }
};

template <typename T> struct func {
    std::vector<bb<T>> bbs;
    idx<bb<T>> start_bb, end_bb;

    template <typename U, typename F> void explore(F f, U init_val, std::optional<edge_state<T, U>> start_state = std::nullopt) {
        std::queue<edge_state<T, U>> to_explore;
        std::unordered_set<edge_state<T, U>> visited;
        std::vector<edge_state<T, U>> possible_states;

        if (start_state) {
            to_explore.push(*start_state);
        } else {
            to_explore.push({0, start_bb, 0, init_val});
        }
        while (!to_explore.empty()) {
            possible_states.clear();

            auto e = to_explore.front();
            to_explore.pop();

            if (auto it = visited.find(e); it != visited.end()) {
                continue;
            }
            visited.insert(e);

            const auto &bb = bbs[*e.bb_idx];

            if (e.bb_idx == end_bb) {
                f(e, bb, { kEnd });
                continue;
            }

            possible_states.push_back(e);
            for (const auto& a: bb.actions) {
                for (auto& es: possible_states) {
                    f(es, bb, a);
                }

                // modify current possible states based on f
                if (a.typ == kLock) {
                    for (auto &es: possible_states) {
                        es.locked_by_task = true;
                    }
                } else if (a.typ == kFallibleLock) {
                    //assert(a.lock_id.has_val());
                    uint32_t mask = (1 << **a.lock_id);

                    if (e.locked_by_task) {
                        // do nothing; the lock can only fail, which is the default state
                    } else {
                        // add the states where the lock was successfully taken
                        //assert(!(es[i].fallible_locks & mask));

                        int len = possible_states.size();
                        for (int i = 0; i < len; i++) {
                            possible_states.push_back({
                                possible_states[i].fallible_locks | mask,
                                possible_states[i].bb_idx,
                                true,
                                possible_states[i].added
                            });
                        }
                    }
                } else if (a.typ == kUnlock) {
                    for (auto &es: possible_states) {
                        es.locked_by_task = false;
                    }
                }
                // NOTE: we ignore calls here; calls should not affect the lock state
                // TODO add support for lock helper
            }

            // propagate to the next basic block
            for (auto& es: possible_states) {
                if (bb.next.depends_on.has_value()) {
                    idx<fallible_lock> i = *bb.next.depends_on;
                    if (es.fallible_locks & (1 << *i)) {
                        to_explore.push(edge_state<T, U>{es.fallible_locks, bb.next.on_true, es.locked_by_task, es.added});
                    } else {
                        to_explore.push(edge_state<T, U>{es.fallible_locks, *bb.next.on_false, es.locked_by_task, es.added});
                    }
                } else {
                    to_explore.push(edge_state<T, U>{es.fallible_locks, bb.next.on_true, es.locked_by_task, es.added});
                    if (bb.next.on_false.has_value()) {
                        to_explore.push(edge_state<T, U>{es.fallible_locks, *bb.next.on_false, es.locked_by_task, es.added});
                    }
                }
            }
        }
    }

    // this shortcuts having to re explore the function if it's called from somewhere else;
    // if it always does blocking take we know it'll fail if the calling function has already taken the lock
    std::optional<bool> saved_always_blocks;

    bool always_blocks() {
        if (saved_always_blocks.has_value()) {
            return *saved_always_blocks;
        }

        bool a_b = true;
        explore<bool>(false, [&](edge_state<T, bool>& es, const bb<T>& basic_block, const action<T>& a) {
            if (a.typ == action_type::kLock) {
                es.added = true;
            }
            if (a.typ == action_type::kEnd) {
                if (!es.added) {
                    a_b = false;
                }
            }
        });

        saved_always_blocks = a_b;
        return a_b;
    }
};

}

template<typename T, typename U> struct std::hash<lock_checker::edge_state<T, U>> {
    std::size_t operator()(const lock_checker::edge_state<T, U>& es) const {
        std::size_t a = std::hash<uint32_t>{}(es.fallible_locks);
        std::size_t b = std::hash<int>{}(*es.bb_idx);
        std::size_t c = std::hash<bool>{}(es.locked_by_task);
        return a ^ (b << 1) ^ (c << 2);
    }
};


