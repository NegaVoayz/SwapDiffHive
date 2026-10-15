#pragma once
#include <vector>
#include "Def.hh"

// A doubly linked list of free slots, usable on its own as the allocation layer
// of a plain hive: get_empty_slot() reuses a free slot and grows the structure
// by one when it is full, while peek_empty_slot() only probes for a free slot.
// free()/use() are non-definitive (idempotent) so a caller may repeat them;
// flip() is the definitive toggle the diff hive relies on.
class FreeChain {
    private:
        struct Node {
            size_t prev;
            size_t next;
        };
        std::vector<Node> link = {};
        size_t free_top = npos;

        size_t next(size_t p) const;
        size_t& next(size_t p);
        size_t prev(size_t p) const;
        size_t& prev(size_t p);

        size_t remove(size_t p);

        size_t insert_before(size_t p, size_t pos);

        size_t new_slot();

        bool on_chain(Id p);

    public:
        size_t get_empty_slot();

        size_t peek_empty_slot() const;

        void free(size_t p);

        void use(size_t p);

        void flip(size_t p);

        void clear();
};
