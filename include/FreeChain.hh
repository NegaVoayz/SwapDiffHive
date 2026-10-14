#pragma once
#include <vector>
#include "Def.hh"

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
        size_t get_empty_slot(bool no_enlarge = false);

        void free(size_t p);

        void use(size_t p);

        void flip(size_t p);
};