## The Swap Diff Protocol

A general protocol for any struct without pointers to keep a certain length of trace for undo/redo.

(sometimes pointers inside will work but make sure they are under strict control)

### Core Design

- Constraints: linear history; no branching.
- Reversibility: applying the same Diff twice cancels its effect.
- History retention: unbounded by default; optional compaction limits rollback depth.
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

#### Update Example

| Time | Operation                          | Data | data book | Temp  | temp book |
| ---- | ---------------------------------- | ---- | --------- | ----- | --------- |
| 1    | initial                            | A    | 0         | \     | 0         |
| 2    | replace A with B(prepare)          | A    | 0         | **B** | 0         |
| 2    | replace A with B(apply)            | *B*  | 0         | *A*   | **2**     |
| 3    | replace A with B(apply) *roll back | *A*  | 0         | *B*   | **0**     |

In the normal data array, book means 0(in use) and x(removed at x),

while in the temp array, book means 0(invalid) and x(removed at x).

This inverse in meaning of **0** makes the symmetric swap possible.

#### Virtual Slots and Linear History

The prepared data is written into a slot whose `book` remains 0, so the allocator still considers the slot empty and available for allocation.

Thus, the prepared data is invisible to the allocation mechanism: it consumes no additional allocated capacity, because it occupies a slot that was already available, or at most converts unallocated capacity into allocated capacity without reducing the available capacity.

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

#### Storage-Ordered Diff Semantics

The order of Diff execution is determined by the storage state transitions and the execution sequence, rather than by the numerical ordering or uniqueness of Diff identifiers. Diff identifiers may be reused or shared by multiple atomic steps. When compaction is enabled, identifiers must preserve the ordering required by the compaction policy.

#### Structure

```C++
template<typename T>
struct Slot {
    T data;
    size_t book;
};
/* Data book:
 * book == 0 --> this slot is occupied (in use)
 * book != 0 --> this slot's data was marked deleted at diff <book> (recoverable)
 * Free Data:
 * A free stack that points to all free slots and possiblly some occupied.
 */
size_t free_data_top = -1;
size_t full_data_top = -1;
chunk_array<size_t> data_link;
chunk_array<Slot<T>> data;
/* Temp book:
 * book == 0 --> this slot is empty (data permanently unavailable)
 * book != 0 --> this slot's data was marked deleted at diff <book> (recoverable)
 * Free Temp:
 * A free stack that points to all free slots and possiblly some occupied.
 */
size_t free_temp_top = -1;
size_t full_temp_top = -1;
chunk_array<size_t> temp_link;
chunk_array<slot<T>> temp;
```

#### Operations

Below are the operations, but surprisingly, all `apply_xxx` here are correct for both redo & undo for a diff.

* Remove

  ```C++
  Diff::Remove prepare_remove(Id diff_id, Id data_id) {
      if (data_id >= data.size()) std::terminate();
      data_link.free(data_id);
      return Diff::Remove{diff_id, data_id};
  }
  
  void apply_remove(const Diff::Remove& remove) {
      data[remove.data_id].book ^= remove.diff_id;
  }
  ```

* Insert

  ```C++
  Diff::Insert prepare_insert(Id diff_id, T&& payload) {
      Id data_id = data_link.get_empty_slot();
      Id temp_id = temp_link.get_empty_slot();
      if (data_id == data.size()) data.emplace_back(T{}, 0);

      if (temp_id == temp.size()) temp.emplace_back(std::move(payload), 0);
      else temp[temp_id] = Slot{std::move(payload), 0};

      data_link.use(data_id);            
      return Diff::Insert{diff_id, data_id, temp_id};
  }
  
  void apply_insert(const Diff::Insert& insert) {
      std::swap(data[insert.data_id], temp[insert.temp_id]);
  }
  ```

* Compact

  Trigger when new diff is added.

  ```C++
  // input: the earliest diff NO that could be traced
  bool compact_slots(size_t trace_limit) {
      if (current_pos != history.size()-1) return false;
      for (Id i = 0; i < temp.size(); ++i)
          if (temp[i].book && temp[i].book < trace_limit) {
              temp[i].book = 0;
              temp_link.free(i);
          }
      return true;
  }
  ```

  

