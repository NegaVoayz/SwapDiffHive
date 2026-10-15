// Protocol tests for SwapDiffHive, following docs/The Swap Diff Protocol.md.
// Registered with ctest as "hive_protocol".
#include "SwapDiffHive/SwapDiffHive.hh"
#include <cstdio>
#include <iterator>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Hive = SwapDiffHive<std::string>;

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

std::string idstr(Id v) { return v == npos ? "npos" : std::to_string(v); }

// A data slot is live while its book is 0 (doc: "book == 0 --> occupied").
std::map<Id, std::string> live(const Hive& h) {
    std::map<Id, std::string> slots;
    for (size_t i = 0; i < h.test_data().size(); ++i)
        if (h.test_data()[i].book == 0) slots[i] = h.test_data()[i].data;
    return slots;
}

// The whole observable state, to compare a replay against an incremental build.
std::string state(const Hive& h) {
    std::string s;
    for (size_t i = 0; i < h.test_data().size(); ++i)
        s += "d" + idstr(i) + "=" + h.test_data()[i].data + "/" + idstr(h.test_data()[i].book) + ";";
    for (size_t i = 0; i < h.test_temp().size(); ++i)
        s += "t" + idstr(i) + "=" + h.test_temp()[i].data + "/" + idstr(h.test_temp()[i].book) + ";";
    return s;
}

// The table in "Insert Example": remove A, insert B, then roll that back.
void doc_insert_example() {
    std::printf("[doc Insert Example]\n");
    Hive h;
    const Id a = h.insert(1, "A");
    check(a == 0 && h.test_data()[0].book == 0, "initial: A occupies slot 0");

    h.remove(2, a);
    check(h.test_data()[0].book == 2, "remove A: book == 2 (removed at diff 2)");

    const Id b = h.insert(3, "B");
    check(b == a, "insert B reuses the slot the removal freed");
    check(h.test_data()[0].data == "B" && h.test_data()[0].book == 0, "applied: data = B, book 0");
    check(h.test_temp()[0].data == "A" && h.test_temp()[0].book == 2, "applied: temp = A, book 2");

    h.seek(2);
    check(h.test_data()[0].data == "A" && h.test_data()[0].book == 2, "rolled back: data = A, book 2");
    check(h.test_temp()[0].data == "B" && h.test_temp()[0].book == 0, "rolled back: temp = B, book 0");
}

// Every op commits as soon as it is prepared, so no seek is needed in between.
void instant_commit() {
    std::printf("[instant commit]\n");
    Hive h;
    const Id a = h.insert(1, "A");
    const Id b = h.insert(2, "B");
    const Id c = h.insert(3, "C");
    check(a != b && b != c, "three inserts in a row: each got its own slot");
    check(live(h).size() == 3, "three live elements");
    check(state(h).find("C") != std::string::npos, "the last payload is stored");

    Hive g;
    g.insert(1, "A");
    g.remove(2, 0);
    g.insert(3, "B");
    check(g.test_data()[0].data == "B" && g.test_data()[0].book == 0, "insert/remove/insert: B live in slot 0");
    check(g.test_temp()[0].data == "A" && g.test_temp()[0].book == 2, "A parked in temp with book 2");
    check(live(g).size() == 1, "exactly one live element");
}

// Rolling back and forth, through a full undo and a replay to the head.
void undo_redo() {
    std::printf("[undo/redo]\n");
    Hive h;
    for (Id i = 1; i <= 3; ++i) h.insert(i, std::string(1, char('A' + i - 1)));
    h.remove(4, 1);
    h.insert(5, "D");
    const std::string head = state(h);

    h.seek(4);
    check(h.test_data()[1].data == "B" && h.test_data()[1].book == 4, "roll back to diff 4: B is deleted again");
    h.seek(3);
    check(live(h).count(1) == 1 && live(h)[1] == "B", "roll back to diff 3: B is live again");

    h.seek(0);
    check(live(h).empty(), "full undo: nothing live");
    bool temp_empty = true, data_deleted = true;
    for (size_t i = 0; i < h.test_temp().size(); ++i) temp_empty &= h.test_temp()[i].book == 0;
    for (size_t i = 0; i < h.test_data().size(); ++i) data_deleted &= h.test_data()[i].book != 0;
    check(temp_empty, "full undo: every temp slot is empty (book 0)");
    check(data_deleted, "full undo: every data slot is marked deleted");

    h.seek(5);
    check(state(h) == head, "redo to head: identical state");
}

// Neighbouring diffs sharing an id form one transaction.
void same_id_transaction() {
    std::printf("[transaction sharing a diff id]\n");
    Hive h;
    h.insert(1, "A");
    h.remove(2, 0);
    h.insert(2, "B");
    check(h.test_data()[0].data == "B" && h.test_data()[0].book == 0, "both ops applied: B live in slot 0");
    check(h.test_temp()[0].data == "A" && h.test_temp()[0].book == 2, "A parked in temp with the group id");

    h.seek(1);
    check(live(h).size() == 1 && live(h)[0] == "A", "seek(1): the whole group rolls back");
    h.seek(2);
    check(h.test_data()[0].data == "B" && h.test_temp()[0].data == "A", "seek(2): the whole group redoes");
}

// A new op overwrites the deprecated tail instead of re-applying it.
void new_op_after_rollback() {
    std::printf("[new op after a rollback]\n");
    Hive h;
    h.insert(1, "A");
    h.remove(2, 0);
    h.seek(1);
    h.remove(3, 0);
    check(h.test_history().size() == 2, "the rolled-back diff was dropped from history");
    check(h.test_data()[0].book == 3, "only the new removal was applied (book 3)");

    const Id slot = h.insert(4, "D");
    check(slot == 0, "the freed slot is reused");
    check(h.test_temp()[0].data == "A" && h.test_temp()[0].book == 3, "A parked in temp with book 3");
}

// Compaction only runs at the head and drops records older than the trace limit.
void compaction() {
    std::printf("[compaction]\n");
    Hive off;
    off.insert(1, "A");
    off.remove(2, 0);
    off.insert(3, "B");
    off.insert(4, "C");
    off.seek(3);
    check(!off.compact(3), "refuses while the head is not current");
    check(off.test_temp()[0].book == 2, "the refused sweep changed nothing");

    Hive h;
    h.insert(1, "A");
    h.remove(2, 0);
    h.insert(3, "B");
    check(h.compact(3), "compacts at the head");
    check(h.test_temp()[0].book == 2, "the record the remaining diff needs is kept");
    check(h.test_history().size() == 1, "the history older than the limit is wiped");
    check(h.test_position() == 0, "current_pos tracks the wiped prefix");
}

// Compaction wipes what lies below the limit. The diffs that remain keep the
// records they need, so rolling back below the wiped ids still lands on a real
// state instead of fabricating one: A stays deleted, it does not come back live.
void rollback_past_compaction() {
    std::printf("[rollback past the compaction boundary]\n");
    Hive h;
    h.insert(1, "A");
    h.remove(2, 0);
    h.insert(3, "B");
    check(h.compact(3), "compaction runs at the head");
    check(h.test_history().size() == 1, "the ids below the limit are wiped");

    h.seek(2);
    check(live(h).empty(), "rolling back below the limit undoes the remaining diffs");
    check(h.test_data()[0].data == "A" && h.test_data()[0].book == 2,
          "A is restored as deleted at diff 2, not as live");
    check(h.test_temp()[0].data == "B" && h.test_temp()[0].book == 0, "B is parked in temp");

    h.seek(3);
    check(live(h).size() == 1 && live(h).at(0) == "B", "seeking back up re-applies the remaining diff");
}

// Random ops checked against a model of the live set, with undo/redo hops.
void randomized_stress() {
    std::printf("[randomized stress: 300 ops]\n");
    Hive h;
    std::mt19937 rng(20261014u);
    std::map<Id, std::string> model, checkpoint;
    Id next = 0;
    bool live_ok = true, round_ok = true;
    for (int step = 0; step < 300; ++step) {
        ++next;
        if (model.empty() || rng() % 3 != 0) {
            const Id slot = h.insert(next, "v" + std::to_string(step));
            model[slot] = "v" + std::to_string(step);
        } else {
            auto it = model.begin();
            std::advance(it, static_cast<long>(rng() % model.size()));
            h.remove(next, it->first);
            model.erase(it);
        }
        if (live(h) != model) {
            live_ok = false;
            std::printf("    step %d: live slots disagree with the model\n", step);
            break;
        }
        checkpoint[next] = state(h);
        if (rng() % 3 == 0 && next > 1) {
            const Id back = 1 + rng() % next;
            h.seek(back);
            h.seek(next);
            if (state(h) != checkpoint[next]) {
                round_ok = false;
                std::printf("    step %d: undo to %s and back changed the state\n", step, idstr(back).c_str());
                break;
            }
        }
    }
    check(live_ok, "live slots always match the model");
    check(round_ok, "undo to an earlier diff and back reproduces the state");
}

// Compaction must keep a record that a remaining diff still needs, even when its
// book is below the limit: with two free slots the reuse order decouples the
// parked book from the neighbouring diff ids.
void compact_keeps_needed_records() {
    std::printf("[compaction keeps needed records]\n");
    Hive h;
    h.insert(1, "A");
    h.insert(2, "B");
    h.remove(3, 0);
    h.remove(4, 1);
    h.insert(5, "C");   // reuses slot 1, parks B(4) in temp[0]
    h.insert(6, "D");   // reuses slot 0, parks A(3) in temp[1]
    check(h.compact(5), "compaction runs at the head");
    check(h.test_history().size() == 2, "only the ids below the limit are wiped");
    check(h.test_temp()[1].book == 3, "the record diff 6 parked is kept although its book is below the limit");

    h.undo();
    check(live(h).size() == 1 && live(h).at(1) == "C", "undoing diff 6 leaves C live");
    check(h.test_data()[0].data == "A" && h.test_data()[0].book == 3, "A comes back deleted at diff 3, not live");

    Hive g;
    g.insert(1, "A");
    g.insert(2, "B");
    g.remove(3, 0);
    g.remove(4, 1);
    g.insert(5, "C");
    g.insert(6, "D");
    check(g.compact(6), "compaction to the newest diff");
    check(g.test_temp()[0].book == 0, "the record of the wiped diff 5 is dropped");
    check(g.test_temp()[1].book == 3, "the record of the remaining diff 6 is kept");
}

// The container is a template parameter, so the protocol must not depend on it.
void vector_container() {
    std::printf("[std::vector as the container]\n");
    SwapDiffHive<std::string, std::vector> h;
    const Id a = h.insert(1, "A");
    h.remove(2, a);
    const Id b = h.insert(3, "B");
    check(b == a && h.test_data()[0].data == "B" && h.test_data()[0].book == 0, "same behaviour as the deque default");
    h.seek(2);
    check(h.test_data()[0].data == "A" && h.test_data()[0].book == 2, "rollback works on std::vector");
    h.seek(3);
    check(h.compact(3), "compaction works on std::vector");
    check(h.test_history().size() == 1 && h.test_temp()[0].book == 2,
          "wiping and record protection work on std::vector");
}

// undo()/redo() step one transaction at a time and never land inside a group.
void undo_redo_ops() {
    std::printf("[undo()/redo()]\n");
    Hive h;
    h.insert(1, "A");
    h.remove(2, 0);
    h.insert(2, "B");   // diffs 2 and 2 are one transaction
    h.insert(3, "C");
    check(live(h).size() == 2, "two live elements");

    h.undo();
    check(live(h).size() == 1 && live(h).at(0) == "B", "undo steps back one transaction");
    h.undo();
    check(live(h).size() == 1 && live(h).at(0) == "A", "undo steps over the whole id-2 group at once");
    h.undo();
    check(live(h).empty(), "undo again reaches the initial state");
    h.undo();
    check(live(h).empty(), "undo at the bottom is a no-op");

    h.redo();
    check(live(h).size() == 1 && live(h).at(0) == "A", "redo re-applies the first transaction");
    h.redo();
    check(live(h).size() == 1 && live(h).at(0) == "B", "redo re-applies the group");
    h.redo();
    check(live(h).size() == 2, "redo reaches the head");
    h.redo();
    check(live(h).size() == 2, "redo at the head is a no-op");
}

// 0 is not a diff id, so it asks for the beginning: every diff that is left is
// rolled back, and the state it lands on is real rather than fabricated.
void rollback_to_beginning() {
    std::printf("[rollback to the beginning]\n");
    Hive h;
    for (Id i = 1; i <= 4; ++i) h.insert(i, std::string(1, char('A' + i - 1)));
    h.seek(0);
    check(live(h).empty(), "without compaction every diff is rolled back");

    Hive g;
    g.insert(1, "A");
    g.remove(2, 0);
    g.insert(3, "B");
    g.insert(4, "C");
    check(g.compact(3), "compacts to the earliest traceable diff");
    check(g.test_history().size() == 2, "the ids below the limit are wiped");

    g.seek(0);
    check(g.test_position() == npos, "every remaining diff is rolled back");
    check(live(g).empty(), "nothing is live, A included");
    check(g.test_data()[0].data == "A" && g.test_data()[0].book == 2, "A is restored as deleted");

    g.undo();
    check(g.test_position() == npos, "there is nothing left to undo");
    g.redo();
    check(g.test_position() == 0 && live(g).size() == 1, "redo re-applies the first remaining diff");

    Hive k;
    k.insert(1, "A");
    k.remove(2, 0);
    k.insert(3, "B");
    k.insert(4, "C");
    k.compact(3);
    k.seek(2);
    check(k.test_position() == npos && live(k).empty(),
          "a target below the wiped ids lands at the start of what is left");
    check(k.test_data()[0].data == "A" && k.test_data()[0].book == 2,
          "and it is a real state: A deleted, nothing live");
}

// One step is one whole block, keyed by the diff ids: with the history
// 1 1 2 2 4 4 4 sitting at the end of the id-2 block, redo must land past the
// three id-4 diffs and undo must land at the end of the id-1 block.
void block_steps() {
    std::printf("[one block per step]\n");
    Hive h;
    h.insert(1, "a"); h.insert(1, "b");
    h.insert(2, "c"); h.insert(2, "d");
    h.insert(4, "e"); h.insert(4, "f"); h.insert(4, "g");
    check(live(h).size() == 7, "seven diffs, seven live elements");

    h.seek(2);                                          // 1 1 2 2(p) 4 4 4
    check(h.test_position() == 3 && live(h).size() == 4, "parked at the end of the id-2 block");

    h.redo();                                           // 1 1 2 2 4 4 4(p)
    check(h.test_position() == 6 && live(h).size() == 7, "redo moves over the whole id-4 block");

    h.seek(2);
    h.undo();                                           // 1 1(p) 2 2 4 4 4
    check(h.test_position() == 1 && live(h).size() == 2, "undo moves over the whole id-2 block");

    h.undo();
    check(h.test_position() == npos && live(h).empty(), "undo past the first block leaves nothing applied");
    h.undo();
    check(h.test_position() == npos, "undo at the bottom is a no-op");
    h.redo();
    check(h.test_position() == 1 && live(h).size() == 2, "redo from the bottom re-applies the first block");
}

// clear_history() drops the rollback trail but keeps the state; clear() forgets
// everything, including the slots and their free lists.
void clearing() {
    std::printf("[clear_history() and clear()]\n");
    Hive h;
    h.insert(1, "A");
    const Id b = h.insert(2, "B");
    h.remove(3, b);
    h.insert(4, "C");                       // recycles B's slot and parks the old payload
    check(h.test_history().size() == 4 && live(h).size() == 2, "four diffs behind two live elements");

    h.clear_history();
    check(h.test_history().empty(), "the history is gone");
    check(h.test_position() == npos, "and there is no position left in it");
    check(h.test_temp().empty(), "the temp deque is emptied, records included");
    check(live(h).size() == 2, "the state itself is untouched");
    check(live(h).count(b) == 1 && live(h)[b] == "C", "the recycles slot still holds C");

    h.insert(5, "D");
    check(h.test_history().size() == 1 && h.test_position() == 0, "a new op starts the history again");
    h.undo();
    check(live(h).size() == 2, "and that op is undoable in turn");
    h.redo();

    h.clear();
    check(h.test_data().empty() && h.test_temp().empty() && h.test_history().empty(), "clear() empties all three");
    check(h.test_position() == npos, "with nothing applied");
    const Id fresh = h.insert(1, "A");
    check(fresh == 0 && live(h).size() == 1, "the slots and their free lists start over at slot 0");
}

// operator[] is raw slot access; at() only answers for a slot that is in use.
void element_access() {
    std::printf("[operator[] and at()]\n");
    Hive h;
    const Id a = h.insert(1, "A");
    const Id b = h.insert(2, "B");
    check(h[a] == "A" && h[b] == "B", "operator[] reads a live slot");
    h[a] = "A2";
    check(h[a] == "A2", "and it is writable");
    const Hive& ch = h;
    check(ch[b] == "B", "the const overload reads too");
    check(ch.at(b) == "B", "and so does the const at()");

    h.remove(3, a);
    check(h.at(b) == "B", "at() answers for a slot in use");
    bool threw = false;
    try { (void)h.at(a); } catch (const std::out_of_range&) { threw = true; }
    check(threw, "at() throws for a slot that was removed");
    threw = false;
    try { (void)h.at(99); } catch (const std::out_of_range&) { threw = true; }
    check(threw, "at() throws for a slot that does not exist");
    check(h[a] == "A2", "operator[] still reaches the removed slot");

    h.seek(2);
    check(h.at(a) == "A2", "and at() answers again once the slot is back in use");
}

// A transaction groups ops under one diff id: they become one block, commit()
// closes the group, rollback() undoes it, and misuse is refused.
void transactions() {
    std::printf("[transactions]\n");
    Hive h;
    h.insert(1, "A");
    h.begin_transaction(2);
    const Id b = h.insert("B");
    const Id c = h.insert("C");
    h.remove(c);
    h.commit();
    check(h.test_history().size() == 4 && live(h).size() == 2, "four ops recorded, two live");
    check(live(h)[b] == "B" && live(h).count(c) == 0, "and C was removed when the group closed");

    h.undo();
    check(live(h).size() == 1 && live(h)[0] == "A", "the whole group undoes as one block");
    h.redo();
    check(live(h).size() == 2, "and redoes as one block");

    h.begin_transaction(3);
    h.insert("D");
    h.remove(b);
    h.rollback();
    check(live(h).size() == 2 && live(h).count(b) == 1 && live(h)[b] == "B", "rollback undoes the group");
    check(h.test_position() == 3 && h.test_history().size() == 4, "and drops it, back to the pre-transaction history");
    h.redo();
    check(live(h).size() == 2, "so redo() cannot bring the group back");

    h.begin_transaction(4);
    h.insert("E");
    h.commit();
    check(live(h).size() == 3 && h.test_history().size() == 5,
          "the next transaction appends to that history");

    auto refused = [](auto&& fn) {
        try { fn(); return false; } catch (const std::logic_error&) { return true; }
    };
    check(refused([&] { h.insert("X"); }) && refused([&] { h.remove(0); }),
          "the payload-only ops need an open transaction");
    check(refused([&] { h.commit(); }) && refused([&] { h.rollback(); }),
          "and commit() and rollback() need one too");

    h.begin_transaction(5);
    check(refused([&] { h.insert(6, "X"); }) && refused([&] { h.remove(6, 0); }),
          "an op inside a transaction must not bring its own id");
    check(refused([&] { h.begin_transaction(7); }), "a transaction cannot start inside one");
    check(refused([&] { h.seek(1); }) && refused([&] { h.undo(); }) && refused([&] { h.redo(); }),
          "the history cannot be walked inside one");
    check(refused([&] { h.compact(1); }) && refused([&] { h.clear_history(); }) && refused([&] { h.clear(); }),
          "nor dropped inside one");
    h.commit();
    check(h.test_history().size() == 5, "an empty transaction changes nothing");

    check(refused([&] { h.begin_transaction(4); }) && refused([&] { h.begin_transaction(3); }),
          "the transaction id must exceed the current block");
    check(refused([&] { h.begin_transaction(0); }), "and 0 is not a diff id");

    h.begin_transaction(6);
    h.rollback();
    check(live(h).size() == 3 && h.test_position() == 4, "an empty rollback leaves the state alone");
}

} // namespace

int main() {
    doc_insert_example();
    instant_commit();
    undo_redo();
    same_id_transaction();
    new_op_after_rollback();
    compaction();
    compact_keeps_needed_records();
    rollback_past_compaction();
    rollback_to_beginning();
    undo_redo_ops();
    block_steps();
    clearing();
    element_access();
    transactions();
    vector_container();
    randomized_stress();
    std::printf("failures: %d\n", failures);
    return failures ? 1 : 0;
}
