#pragma once

#include <cstdint>
#include <cstdio>

#include <optional>
#include <queue>
#include <unordered_set>
#include <vector>

// almost everything here is templated around a generic variable T, which needs to provide two types,
// - a Location type for mapping a lock, unlock, or call to its location in code,
// - and a FuncId type that provides a unique identifier for the function
// - and a LockId type that provides a unique identifier for the lock (which must be shared/used by all the locks in all the functions
//
// this is genericized so that unit testing can be done on this code without needing to use the representations
// GCC provides; we can mock out gcc's tree and location_t for our own structs in testing

namespace lock_checker {

// everything is parametized around T, which must provide:
// struct T {
//      using Location = /* */;
//      using FuncId = /* */;
//      using LockId = /* */;
// };

template <typename T> struct lock_state {
    uint32_t state;

    lock_state<T> operator&(const lock_state<T>& other) const {
        return {state & other.state};
    }
    lock_state<T> operator&(const uint32_t& other) const {
        return {state & other};
    }
    lock_state<T> operator|(const lock_state<T>& other) const {
        return {state | other.state};
    }
    lock_state<T> operator|(const uint32_t& other) const {
        return {state | other};
    }
    bool operator==(const lock_state<T>& other) const {
        return state == other.state;
    }
    bool operator==(const uint32_t& other) const {
        return state == other;
    }
    bool operator!=(const lock_state<T>& other) const {
        return state != other.state;
    }
    bool operator!=(const uint32_t& other) const {
        return state != other;
    }
    lock_state<T> operator~() const {
        return {~state};
    }
};


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

    // use to set/clear the corresponding bit in a lock_state<T> struct
    lock_state<T> mask() const {
        return { (uint32_t)(1 << idx_) };
    }
};

struct lock;
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
    using Loc = typename T::Location;
    using FH = typename T::FuncId;

    action_type typ;
    Loc loc; // location in code to notify in case of error
    std::optional<idx<lock>> lock_id;
    std::optional<idx<fallible_lock>> call_id; // only used for fallible locks
    std::optional<FH> called_func; // pointer to the function declaration for calls

protected:
    action() = default;
public:
    static action<T> lock_(Loc loc, idx<lock> lock_id) {
        return action<T>{
            .typ = kLock,
            .loc = loc,
            .lock_id = lock_id
        };
    }
    static action<T> fallible_lock_(Loc loc, idx<lock> lock_id, idx<fallible_lock> call_id) {
        return action<T> {
            .typ = kFallibleLock,
            .loc = loc,
            .lock_id = lock_id,
            .call_id = call_id
        };
    }
    static action<T> unlock_(Loc loc, idx<lock> lock_id) {
        return action<T>{
            .typ = kUnlock,
            .loc = loc,
            .lock_id = lock_id
        };
    }
    static action<T> call_(Loc loc, FH func_handler) {
        return action<T>{
            .typ = kCall,
            .loc = loc,
            .called_func = func_handler
        };
    }
    static action<T> end_(Loc loc) {
        return action<T> {
            .typ = kEnd,
            .loc = loc
        };
    }
};

template <typename T> struct cond_edge {
    idx<bb<T>> on_true;
    std::optional<idx<bb<T>>> on_false;
    std::optional<idx<fallible_lock>> depends_on; // index of the fallible lock call this depends on, else use the true edge

    //cond_edge(): on_true() {}
};

template <typename T> struct bb {
    std::vector<action<T>> actions; // actions to do to replay a basic_block
    //typename T::Location loc; // where to alert if a semaphore isn't unlocked properly
    cond_edge<T> next;

    //bb(): end(), next() {}
};

template <typename T, typename U> struct edge_state {
    lock_state<fallible_lock> fallible_locks; // state of fallible lock calls up to this point
    idx<bb<T>> bb_idx;
    lock_state<lock> cur_lock_state;
    U added; // any extra info that the caller wants to add to the state

    bool operator==(const edge_state<T, U>& other) const {
        return (other.fallible_locks == fallible_locks) && (*other.bb_idx == *bb_idx) && (other.cur_lock_state == cur_lock_state);
    }
};

template <typename T> struct func {
    std::vector<typename T::LockId> locks; // need some kind of lock id to map between locks across calls
    std::vector<bb<T>> bbs;
    idx<bb<T>> start_bb, end_bb;

    const typename T::LockId& lookup_lock(const idx<lock>& idx) const {
        return locks[*idx];
    }

    template <typename U, typename F> void explore(F f, U init_val, std::optional<edge_state<T, U>> start_state = std::nullopt) const {
        std::queue<edge_state<T, U>> to_explore;
        std::unordered_set<edge_state<T, U>> visited;
        std::vector<edge_state<T, U>> possible_states;

        if (start_state) {
            to_explore.push(*start_state);
        } else {
            to_explore.push({{0}, start_bb, {0}, init_val});
        }
        while (!to_explore.empty()) {
            possible_states.clear();

            auto e = to_explore.front();
            to_explore.pop();

            if (auto it = visited.find(e); it != visited.end()) {
                continue;
            }
            visited.insert(e);

            //fprintf(stderr, "accessing bb %d\n", *e.bb_idx);
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
                    auto lock_mask = (1 << **a.lock_id);
                    for (auto &es: possible_states) {
                        es.cur_lock_state = es.cur_lock_state | lock_mask;
                    }
                } else if (a.typ == kFallibleLock) {
                    //assert(a.call_id.has_val());
                    auto call_mask = a.call_id->mask();
                    auto lock_mask = a.lock_id->mask();

                    if ((e.cur_lock_state & lock_mask) != 0) {
                        // do nothing; the lock can only fail, which is the default state
                    } else {
                        // add the states where the lock was successfully taken
                        //assert(!(es[i].fallible_locks & mask));

                        int len = possible_states.size();
                        for (int i = 0; i < len; i++) {
                            possible_states.push_back({
                                possible_states[i].fallible_locks | call_mask,
                                possible_states[i].bb_idx,
                                e.cur_lock_state | lock_mask,
                                possible_states[i].added
                            });
                        }
                    }
                } else if (a.typ == kUnlock) {
                    auto lock_mask = a.lock_id->mask();
                    for (auto &es: possible_states) {
                        es.cur_lock_state = es.cur_lock_state & ~lock_mask;
                    }
                }
                // NOTE: we ignore calls here; calls should not affect the lock state
                // TODO add support for lock helper
            }

            // propagate to the next basic block
            for (auto& es: possible_states) {
                if (bb.next.depends_on.has_value()) {
                    idx<fallible_lock> i = *bb.next.depends_on;
                    if ((es.fallible_locks & i.mask()) != 0) {
                        to_explore.push(edge_state<T, U>{es.fallible_locks, bb.next.on_true, es.cur_lock_state, es.added});
                        //fprintf(stderr, "queued bb %d from %d\n", *bb.next.on_true, *e.bb_idx);
                    } else {
                        to_explore.push(edge_state<T, U>{es.fallible_locks, *bb.next.on_false, es.cur_lock_state, es.added});
                        //fprintf(stderr, "queued bb %d from %d\n", **bb.next.on_false, *e.bb_idx);
                    }
                } else {
                    // the conditional does not depend on a fallible lock call (as far as we can tell),
                    // continue on both branches
                    to_explore.push(edge_state<T, U>{es.fallible_locks, bb.next.on_true, es.cur_lock_state, es.added});
                    //fprintf(stderr, "queued bb %d from %d\n", *bb.next.on_true, *e.bb_idx);
                    if (bb.next.on_false.has_value()) {
                        to_explore.push(edge_state<T, U>{es.fallible_locks, *bb.next.on_false, es.cur_lock_state, es.added});
                        //fprintf(stderr, "queued bb %d from %d\n", **bb.next.on_false, *e.bb_idx);
                    }
                }
            }
        }
    }
};

}

template<typename T, typename U> struct std::hash<lock_checker::edge_state<T, U>> {
    std::size_t operator()(const lock_checker::edge_state<T, U>& es) const {
        std::size_t a = std::hash<uint32_t>{}(es.fallible_locks.state);
        std::size_t b = std::hash<int>{}(*es.bb_idx);
        std::size_t c = std::hash<uint32_t>{}(es.cur_lock_state.state);
        return a ^ (b << 1) ^ (c << 2);
    }
};


