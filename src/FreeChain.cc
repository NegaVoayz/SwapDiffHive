#include <vector>
#include "SwapDiffHive/Def.hh"
#include "SwapDiffHive/FreeChain.hh"

size_t FreeChain::next(size_t p) const {return link[p].next;}
size_t& FreeChain::next(size_t p) {return link[p].next;}
size_t FreeChain::prev(size_t p) const {return link[p].prev;}
size_t& FreeChain::prev(size_t p) {return link[p].prev;}

size_t FreeChain::remove(size_t p) {
    size_t pp = prev(p);
    size_t np = next(p);
    if (pp != npos) {
        next(pp) = np;
        prev(p) = npos;
    }
    if (np != npos) {
        prev(np) = pp;
        next(p) = npos;
    }
    return np;
}

size_t FreeChain::insert_before(size_t p, size_t pos) {
    if (next(p) == pos) return p;
    if (pos == npos) {
        next(p) = npos;
        return p;
    }
    size_t pp = prev(pos);
    if (pp != npos) next(pp) = p;
    prev(p) = pp;
    next(p) = pos;
    prev(pos) = p;
    return p;
}

size_t FreeChain::new_slot() {
    link.push_back({npos, npos});
    return free_top = insert_before(link.size()-1, free_top);
}

bool FreeChain::on_chain(Id p) {
    return p == free_top || prev(p) != npos;
}

size_t FreeChain::get_empty_slot() {
    // free_top is npos exactly when the chain is empty: enlarge by one slot
    if (free_top == npos) free_top = new_slot();
    return free_top;
}

size_t FreeChain::peek_empty_slot() const {
    return free_top;
}

void FreeChain::free(size_t p) {
    if (p == npos) return;
    if (on_chain(p)) return;
    free_top = insert_before(p, free_top);
}

void FreeChain::use(size_t p) {
    if (p == npos) return;
    if (!on_chain(p)) return;
    // if p is free_top, we have to update free_top
    if (p == free_top) free_top = next(free_top);
    remove(p);
}

void FreeChain::flip(size_t p) {
    if (p == npos) return;

    if (p == free_top) { free_top = next(free_top); remove(p); }
    else if (prev(p) != npos) { remove(p); }
    else free_top = insert_before(p, free_top);
}

void FreeChain::clear() {
    link.clear();
    free_top = npos;
}
