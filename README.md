# SwapDiffHive

A C++23 hive that keeps a trace of undo/redo diffs for a movable payload type,
following the swap-diff protocol described in
[docs/The Swap Diff Protocol.md](docs/The%20Swap%20Diff%20Protocol.md).

## Requirements

- CMake 3.20 or newer, and a C++23 toolchain (the headers use
  `std::bind_front` and `std::unreachable`): GCC 12+, Clang 15+ or MSVC 19.32+.
  The language level travels with the imported target, so a consumer does not
  have to set one.
- A payload type that is **movable** — move constructible and move assignable.
  It does not have to be copyable or default constructible.

Payloads are moved between slots, never copied, so `std::unique_ptr` works and
ownership is never duplicated. What a payload must tolerate is being
*relocated*: nothing may keep a pointer to it across operations. Removed
payloads stay alive so that a rollback can restore them, so a hive can own more
payloads than it has live elements; they are released when their slot is reused,
when `clear_history()` drops the records, or when the hive is destroyed.

## Using it

Both ways hand the consumer the same target and the same include spelling:

```cmake
find_package(SwapDiffHive 1.0 CONFIG REQUIRED)
target_link_libraries(app PRIVATE SwapDiffHive::SwapDiffHive)
```

```cmake
include(FetchContent)
FetchContent_Declare(SwapDiffHive GIT_REPOSITORY <url> GIT_TAG v1.0.1)
FetchContent_MakeAvailable(SwapDiffHive)
target_link_libraries(app PRIVATE SwapDiffHive::SwapDiffHive)
```

```cpp
#include "SwapDiffHive/SwapDiffHive.hh"

SwapDiffHive<std::string> hive;
const Id slot = hive.insert(1, "A");
hive[slot];              // raw slot access
hive.at(slot);           // the same, but throws std::out_of_range when the slot
                         // is not in use, removed or never handed out
hive.remove(2, slot);
hive.undo();             // one block back
hive.redo();             // and forward again
hive.compact(2);         // forget everything older than diff 2

hive.begin_transaction(3);          // the ops that follow share diff id 3
hive.insert("B");
hive.insert("C");
hive.commit();                      // keep the group; the ops were applied as they came
// hive.rollback();                 // or undo the whole group, drop it, end the transaction
```

Diff ids start at 1 and never decrease; ids that repeat share one block, and a
block moves as a unit, which is what a transaction is. The payload-only
`insert`/`remove` need an open transaction; outside one, pass the diff id
explicitly. `tests/swap_diff_hive_test.cc` documents the rest of the behaviour by
example.

## Building and testing

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

`hive_protocol` covers the protocol, `free_chain` the allocation layer on its
own.
