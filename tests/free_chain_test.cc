// FreeChain driven through its public API only - the way it is used as the
// allocation layer of a plain hive, i.e. independently of SwapDiffHive.
#include "SwapDiffHive/FreeChain.hh"
#include <cstdio>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

} // namespace

int main() {
    FreeChain c;

    check(c.peek_empty_slot() == npos, "empty structure reports nothing free");
    check(c.get_empty_slot() == 0, "first allocation grows slot 0");
    check(c.peek_empty_slot() == 0, "peek reports the free slot without claiming it");
    check(c.get_empty_slot() == 0, "probing repeatedly does not grow the structure");
    c.use(0);
    check(c.peek_empty_slot() == npos, "claiming the slot empties the free list");

    check(c.get_empty_slot() == 1, "next allocation grows slot 1");
    c.use(1);
    check(c.get_empty_slot() == 2, "and then slot 2");
    c.use(2);
    check(c.peek_empty_slot() == npos, "three slots allocated, none free");

    c.free(0);
    check(c.peek_empty_slot() == 0, "free() returns slot 0 to the list");
    c.free(0);
    check(c.peek_empty_slot() == 0, "free() is non-definitive: repeating it is a no-op");
    c.free(1);
    check(c.peek_empty_slot() == 1, "the most recently freed slot becomes the head");
    check(c.get_empty_slot() == 1, "allocation reuses that head");

    c.use(1);
    check(c.peek_empty_slot() == 0, "use() advances to the next free slot");
    c.flip(0);
    check(c.peek_empty_slot() == npos, "flip() on a free slot takes it off the list");
    c.flip(0);
    check(c.peek_empty_slot() == 0, "flip() again puts it back - definitive toggle");
    c.use(0);
    check(c.peek_empty_slot() == npos, "slot 0 claimed");
    c.use(0);
    check(c.peek_empty_slot() == npos, "use() is non-definitive: repeating it is a no-op");

    std::printf("failures: %d\n", failures);
    return failures ? 1 : 0;
}
