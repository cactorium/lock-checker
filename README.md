# FreeRTOS lock checker
This is a GCC plugin to statically analyze code to avoid deadlocks.
This can't be solved in the general case but it seems possible if we make some (many) assumptions

## Stage 1 - Simple case
- Single static global nonrecursive semaphore/mutex
    - So only functions within a file can use them
- The semaphores are used as mutexes to protect invariants, not for synchronizing across multiple tasks.
    - So, using it like a mutex, each semaphore take for a given handle within a function must be followed by a semaphore give for the same handle
    - This equivalently means a semaphore give for a handle can't occur without a semaphore take for that handle happening first
    - This also means that a function must return with the semaphores in the same state as it started
- At most 32 fallible semaphore calls in a given function (preferably much less)
    - This allows the state to be stored in a single integer
- The link between a fallible semaphore call and the next branch is very short and consists only of the return value and simple binary expressions
    - The value calculator can only trace from the branch back to the semaphore under this situation
- The delay values used in the semaphore take calls are constants, and if they're not, they're never portMAX_DELAY
    - Otherwise we can't distinguish fallible from infallible locks, which means we wouldn't be able to detect deadlocks from taking a semaphore twice
- This can't check for functions calling functions in a different file that then call functions in this file

## Stage 2 - More complicated cases
- There are less than 32 static global semaphores in a given file and less than 32 fallible semaphore calls in a given function
    - Preferably there are very few fallible semaphore calls; the time and memory increases exponentially with the number of fallible semaphore calls
    - This is a limitation to simplify the logic needed to track all the states; we're trying to fit it all in a single 64-bit integer
- If there are more than a single mutex, they should only be taken in a given order
    - It's very tricky to determine if it's valid to take them in a different order, it's easy to check to make sure they're always taken in a given order
    - They should be given in the opposite order they were taken (take A, take B, give B, give A)
    - This also means that a function must return with the semaphores in the same state as it started

## Implementation
The overall idea is to do this:
- For each function, trace through every possible path to look for semaphore takes/gives and make sure they don't break the invariants
    - Simulate this for every possible combination of results of fallible semaphore takes/gives
        - Calculate all the states we can end up in, make sure the invariants mentioned above are never broken
    - Conservatively, we assume that the mutexes could be in any combination of states at the start of a function call
        - To deal with this, we simulate the control flow for the function given that the fallible function calls in it could be in 
        - Fallible semaphore calls for a certain mutex can fail (emulating another task holding it), and after it succeeds it can only fail (because the current task is holding it) until the task gives it
        - Theoretically some combinations would be impossible, but that's very hard to determine
            - This is a pain to implement and I'm assuming most of the code I'm planning to use this on has like one or two semaphores
    - If there are function calls we don't recognize, we save the function name to finish evaluating when that function is analyzed
        - Since we assume no function can change the state of the semaphores, we can continue checking a function without know exactly what a function it calls does
            - We'd just miss deadlocks caused by the called function taking the same semaphore as the calling function, but those'd be caught when that function is analyzed and that part of the function is reanalyzed
        - Currently we're doing a conservative estimate, in that we don't fully resimulate the function to ensure the lock is taken twice, we only keep track of which semaphores are taken with a blocking call

## Current progress:
- [x] Compilable GCC plugin
- [x] Implement a custom pass
- [x] Parse the gimple tree to extract function calls, branches, and assignments
- [x] Convert the parsed gimple tree into data for each basic block
- [x] Analyze each function individually by running through the basic blocks
- [x] Extend to cover function calls to other functions defined in the same file
- [x] Refine to stage 2
- [ ] Check mutex ordering
