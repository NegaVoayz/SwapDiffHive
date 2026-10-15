## The Swap Diff Protocol

A general protocol for any struct without pointers to keep a certain length of trace for undo/redo.

(sometimes pointers inside will work but make sure they are under strict control)

### Core Design

- Constraints: linear history; no branching.
- Reversibility: applying the same Diff twice cancels its effect.
- Execution: every op is committed as soon as it is prepared, so a Diff is never left pending.
- Diff ids: start at 1, never decrease along the history, and may repeat. Neighbours that share an id form one block (one transaction). 0 is not an id: `seek(0)` means "the beginning".
- History retention: unbounded by default; `compact()` limits rollback depth.
- Storage semantics: `book` has location-dependent meanings in `data` and `temp`.

> Written data are shown in **bold**, and swapped data are shown in *italic*.

#### Insert Example

| Time | Operation                  | Data | data book | Temp  | temp book |
| ---- | -------------------------- | ---- | --------- | ----- | --------- |
| 1    | initial                    | A    | 0         | \     | 0         |
| 2    | remove A                   | A    | 2         | \     | 0         |
| 3    | insert B(prepare)          | A    | 2         | **B** | 0         |
| 3    | insert B(apply)            | *B*  | *0*       | *A*   | *2*       |
| 4    | insert B(apply) *roll back | *A*  | *2*       | *B*   | *0*       |

In the normal data array, book means 0(in use) and x(removed at x),

while in the temp array, book means 0(invalid) and x(removed at x).

This inverse in meaning of **0** makes the symmetric swap possible.

This is the recycle path: a slot that a removal freed is taken by the next
insert, so the payload that was there has to be parked in `temp`. A brand new
slot has nothing to park and takes the Unary fast path instead.

#### Append Example

| Time | Operation                  | Data  | data book | Temp | temp book |
| ---- | -------------------------- | ----- | --------- | ---- | --------- |
| 1    | insert A(prepare)          | **A** | **1**     | \    | 0         |
| 1    | insert A(apply)            | A     | **0**     | \    | 0         |
| 2    | insert A(apply) *roll back | A     | **1**     | \    | 0         |

The prepared slot is on the free list (a non-zero book is what the data side
reads as "removed", and the free list only holds such slots), applying flips the
book to 0 and takes the slot off the list, rolling back flips both back.

#### Diff Ids and Blocks

Diff ids are the only ordering device:

- they start at 1; a book xor with 0 would be a no-op, so 0 is free to mean "before everything";
- they never decrease along the history, which is what lets `seek(0)`, `undo()` and `redo()` walk it;
- neighbours that share an id are one block, i.e. one transaction: `undo()` and `redo()` move a whole block, and `seek()` stops at a block end;
- `compact()` wipes a prefix of the history, which is only the oldest diffs while the ids do not decrease.

A fresh id after a rollback joins nothing: the rollback already dropped the diffs above the position, so the id it reuses no longer exists.

#### Storage

```C++
template<typename T, template<typename> typename Container = std::deque>
class SwapDiffHive {
    struct Slot {
        T data;
        Id book;
    };

    FreeChain data_link{};       // free list of data slots
    Container<Slot> data{};
    FreeChain temp_link{};       // free list of temp slots
    Container<Slot> temp{};

    Id current_pos = npos;       // index of the newest applied diff; npos means nothing applied
    Container<Diff> history{};
};
```

`FreeChain` is a doubly linked list of free slot numbers: `get_empty_slot()`
reuses the head and grows the structure by one when the list is empty,
`peek_empty_slot()` only probes for a free slot, `free()`/`use()` are
non-definitive (repeating them changes nothing) and `flip()` is the definitive
toggle this hive is built on.

The lists and the books always agree:

- a data slot is on the data free list **iff** its book is non-zero;
- a temp slot is on the temp free list **iff** its book is zero.

#### Operations

Below are the operations, but surprisingly, all `apply_xxx` here are correct for
both redo & undo for a diff: each one is its own inverse, so running it twice
restores the slots, the payloads and the free lists.

* Remove

  ```C++
  Diff prepare_remove(Id diff_id, Id data_id) {
      if (data_id >= data.size()) std::terminate();
      return to_diff(Diff::Unary{diff_id, data_id});
  }

  void apply_unary(const Diff::Unary& unary) {
      data[unary.data_id].book ^= unary.diff_id;
      data_link.flip(unary.data_id);
  }
  ```

* Insert

  ```C++
  Diff prepare_insert(Id diff_id, T&& payload) {
      Id data_id = data_link.get_empty_slot();
      // append fast path: a brand new slot has no old payload to park, so
      // flipping the book is cheaper than burning a temp slot
      if (data_id == data.size()) {
          data.emplace_back(std::move(payload), diff_id);
          return to_diff(Diff::Unary{diff_id, data_id});
      }

      // emplace when the temp array grows: resize() would value-initialise
      // the new slot and with it require T to be default constructible
      Id temp_id = temp_link.get_empty_slot();
      if (temp_id == temp.size()) temp.emplace_back(std::move(payload), 0);
      else temp[temp_id] = Slot{std::move(payload), 0};

      return to_diff(Diff::Binary{diff_id, data_id, temp_id});
  }

  void apply_binary(const Diff::Binary& binary) {
      std::swap(data[binary.data_id], temp[binary.temp_id]);
      data_link.flip(binary.data_id);
      temp_link.flip(binary.temp_id);
  }
  ```

  A fresh data slot is prepared with `book == diff_id`, which the data side reads
  as "removed at diff_id", while the slot is still on the free list; the apply
  flips that to 0 and takes it off. A recycled slot keeps its own book until the
  swap carries the new payload's book (0) into it.

* Commit

  ```C++
  void commit(Diff diff) {
      history.resize(current_pos + 1);   // drop the deprecated tail
      history.push_back(diff);
      ++current_pos;
      apply_diff(history[current_pos]);
  }
  ```

  A new Diff always lands at `current_pos + 1`: on the head that is a plain
  `push_back`, after a rollback it overwrites the tail the rollback deprecated.
  Every op goes through this, so nothing stays pending and a second op can never
  be handed a slot the first one prepared.

* Seek, Undo, Redo

  ```C++
  void seek(Id diff_id) {
      while (current_pos != npos && history[current_pos].common.diff_id > diff_id) {
          apply_diff(history[current_pos]);
          --current_pos;
      }
      while (current_pos + 1 < history.size() && history[current_pos + 1].common.diff_id <= diff_id) {
          ++current_pos;
          apply_diff(history[current_pos]);
      }
  }
  ```

  `seek()` walks to the end of the last block whose id is `<= diff_id`: there is
  no search, the walks are the work. `seek(0)` rolls back everything that is
  left, and so does any id below the wiped ones.

  `undo()` and `redo()` step exactly one block: `undo()` hands back the block
  sitting at the position, `redo()` re-applies the block queued after it. Both
  walk the history itself, so no fabricated diff id is ever passed around.

  ```C++
  void undo() {
      if (current_pos == npos) return;                       // nothing applied
      const Id block = history[current_pos].common.diff_id;
      while (current_pos != npos && history[current_pos].common.diff_id == block) {
          apply_diff(history[current_pos]);
          --current_pos;
      }
  }

  void redo() {
      if (current_pos + 1 >= history.size()) return;         // nothing queued
      const Id block = history[current_pos + 1].common.diff_id;
      while (current_pos + 1 < history.size() && history[current_pos + 1].common.diff_id == block) {
          ++current_pos;
          apply_diff(history[current_pos]);
      }
  }
  ```

* Compact

  ```C++
  // input: the earliest diff NO that could still be traced
  bool compact(size_t trace_limit) {
      if (current_pos + 1 != history.size()) return false;   // only at the head

      const auto first_kept = std::lower_bound(
          history.begin(), history.end(), static_cast<Id>(trace_limit),
          [](const Diff& d, Id limit) { return d.common.diff_id < limit; });

      // a remaining diff still needs the temporary record it parked, no matter
      // how old the book on it is
      Container<char> needed(temp.size(), 0);
      for (auto it = first_kept; it != history.end(); ++it)
          if (it->kind == Diff::Kind::Binary) needed[it->binary.temp_id] = 1;

      for (Id i = 0; i < temp.size(); ++i)
          if (!needed[i] && temp[i].book && temp[i].book < trace_limit) {
              temp[i].book = 0;
              temp_link.free(i);
          }

      current_pos -= static_cast<Id>(std::distance(history.begin(), first_kept));
      history.erase(history.begin(), first_kept);
      return true;
  }
  ```

  Trigger it yourself when the window should advance; nothing calls it
  automatically, and it only works at the head. It wipes the history below
  `trace_limit`, frees the temp records nothing left needs and shifts
  `current_pos` by the wiped count. Everything that remains is still undoable,
  so the oldest reachable state is the start of what is left.

  Dropping the records by their book alone is not enough: a parked record's book
  is the removal id of the payload it holds, and with more than one free slot
  the reuse order decouples that book from the neighbouring diff ids, so a
  remaining diff can need a record whose book is already below the limit.
  Dropping it would make that diff's undo restore a payload with a zeroed book,
  i.e. bring deleted data back as live. Hence the `needed` pass.

* Clear

  ```C++
  // the state stays, it becomes the new baseline
  void clear_history() {
      history.clear();
      current_pos = npos;
      temp.clear();
      temp_link.clear();
  }

  // forget everything, as if the hive had just been constructed
  void clear() {
      clear_history();
      data.clear();
      data_link.clear();
  }
  ```

  `clear_history()` drops the diffs and the temp records they would need, so
  nothing can be rolled back any more while the live payloads stay exactly where
  they are; the current state simply becomes the new baseline. `clear()` goes
  further and leaves a freshly constructed hive. Both have to reset the free
  list that belongs to the container they empty, otherwise it would hand out
  slot numbers that no longer exist. Note that `clear_history()` leaves the
  deleted slots on the data free list: their stale payloads are released when a
  later insert moves a new one in.

#### Virtual Slots and Linear History

The prepared data is written into a slot that is still on the free list, so the
allocator considers the slot empty and available: a Binary insert parks its
payload in a temp slot whose `book` is 0, and the Unary fast path prepares into
the data slot with `book == diff_id`. Either way the prepared data is invisible
to the allocation mechanism: it consumes no additional allocated capacity,
because it occupies a slot that was already available, or at most converts
unallocated capacity into allocated capacity without reducing the available
capacity.

Then when applied, that slot is used to contain the old slot to be removed.

The correctness is based on the linear history assumption: 

1. The virtual slot only exists when current Diff is reverted.

2. No Diff can set a temp book to 0 when moving forward, 

   so this virtual slot must be empty from the beginning till this Diff, 

   which means that this Diff is the **only one** that has written something into the slot data.

   Thus a former Diff never writes this slot.

3. A Diff after this Diff will see a completed swap so the virtual slot would be a solid slot with **non-zero** book value.

   Thus a future Diff never writes this slot.

Finally, this virtual slot won't be overwritten unless its owner Diff is applied, in which case we don't need to worry.

Or a new operation has applied a new Diff, but in that case the Diff we focus on will be deprecated since there's only linear history, the virtual slot will never be used again.

### Notes

- `Container` is a template parameter and defaults to `std::deque`: the slots and the history only ever grow or shrink at the tail, which a deque does without moving the stored payloads.
- `tests/` covers the protocol: `ctest` runs `free_chain` (the allocation layer on its own) and `hive_protocol` (the examples above, the linear-history invariants, block stepping, compaction and a randomized stress run checked against a model).
