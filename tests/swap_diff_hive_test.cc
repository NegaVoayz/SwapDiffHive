// Protocol tests for SwapDiffHive, following docs/The Swap Diff Protocol.md.
//
// The suite runs two groups:
//   * the protocol tests, registered with ctest as "hive_protocol"
//   * a characterization of one open issue, only run with --known-issues and
//     registered as a disabled test until the issue is addressed
#include "SwapDiffHive.hh"
#include <cstdio>
#include <iterator>
#include <map>
#include <random>
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
    check(!off.compact_slots(3), "refuses while the head is not current");
    check(off.test_temp()[0].book == 2, "the refused sweep changed nothing");

    Hive h;
    h.insert(1, "A");
    h.remove(2, 0);
    h.insert(3, "B");
    check(h.compact_slots(3), "compacts at the head");
    check(h.test_temp()[0].book == 0, "a record older than the trace limit is dropped");
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

// Open issue: compaction discards the record of the removal, but seek() still
// walks past the boundary. The undo re-swaps a slot the allocator already
// reclaimed, so the deleted element comes back live and the state no longer
// describes any point of the history. Once the boundary is respected - refused,
// or clamped to the trace limit - the state must stay at the boundary itself,
// which is what this asserts.
void known_issue_rollback_past_compaction() {
    std::printf("[known issue: rollback past the compaction boundary]\n");
    Hive h;
    h.insert(1, "A");
    h.remove(2, 0);
    h.insert(3, "B");
    check(h.compact_slots(3), "compaction runs at the head");
    h.seek(2);
    const auto live_now = live(h);
    check(live_now.size() == 1 && live_now.count(0) == 1 && live_now.at(0) == "B",
          "the state stays at the boundary: B live, A still deleted");
    check(h.test_temp()[0].book == 0, "the temp slot stays empty");
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--known-issues") {
        known_issue_rollback_past_compaction();
    } else {
        doc_insert_example();
        instant_commit();
        undo_redo();
        same_id_transaction();
        new_op_after_rollback();
        compaction();
        randomized_stress();
    }
    std::printf("failures: %d\n", failures);
    return failures ? 1 : 0;
}
