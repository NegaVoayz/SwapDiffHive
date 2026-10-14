#include <vector>
#include "def.hh"

template<typename IsFree>
class FreeChain {
    private:
        struct Node {
            size_t prev;
            size_t next;
        };
        std::vector<Node> link = {};
        size_t free_top = npos;
        size_t free_end = npos;
        const IsFree is_free;

        size_t next(size_t p) const {return link[p].next;}
        size_t& next(size_t p) {return link[p].next;}
        size_t prev(size_t p) const {return link[p].prev;}
        size_t& prev(size_t p) {return link[p].prev;}
        size_t full_top() const {return free_end == npos ? free_top : next(free_end);}

        size_t remove(size_t p) {
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

        size_t insert_before(size_t p, size_t pos) {
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

        size_t insert_after(size_t p, size_t pos) {
            if (prev(p) == pos) return p;
            if (pos == npos) {
                prev(p) = npos;
                return p;
            }
            size_t np = next(pos);
            if (np != npos) prev(np) = p;
            next(p) = np;
            prev(p) = pos;
            next(pos) = p;
            return p;
        }

        size_t scan_free_chain() {
            while(free_end != npos) {
                if (is_free(free_top)) {
                    return free_top;
                }
                // now we know free_top points to a full slot
                if (free_top == free_end) {
                    free_end = npos;
                } else {
                    size_t p = free_top;
                    free_top = remove(p);
                    insert_after(p, free_end);
                }
            }
            return npos;
        }

        size_t scan_full_chain() {
            if (full_top() == npos) return npos;
            // try first slot
            if (is_free(full_top())) {
                return free_end = full_top();
            }
            // or try others
            size_t p = next(full_top());
            while(p != npos) {
                if (is_free(p)) {
                    remove(p);
                    free_top = insert_before(p, free_top);
                    if (free_end == npos) free_end = free_top;
                    return p;
                }
                p = next(p);
            }
            return npos;
        }

        size_t new_slot() {
            link.push_back({npos, free_top});
            if (free_top == npos)
                return free_top = free_end = link.size()-1;
            return free_top = (prev(free_top) = link.size()-1);
        }
    public:
        FreeChain(IsFree is_free) : is_free(is_free) {}

        size_t get_empty_slot(bool no_enlarge = false) {
            // scan empty chain then scan full chain
            // if still no empty slot found, return npos
            size_t p;
            p = scan_free_chain();
            if (p != npos) return p;
            p = scan_full_chain();
            if (no_enlarge || p != npos) return p;
            return new_slot();
        }

        void free(size_t p) {
            if (p == npos) return;

            if (p == free_top) {
                if (free_end == npos)
                    free_end = full_top();
                return;
            }

            // if p is full_top, simply update full_top since anything before full_top is free
            if (p == full_top()) {
                free_end = full_top();
                return;
            }

            remove(p);
            free_top = insert_before(p, free_top);
            if (free_end == npos) free_end = free_top;
        }

        void use(size_t p) {
            if (free_end == npos) return;

            if (p == npos) return;

            if (p == full_top()) return;
            
            // if p is just before full_top, simply move full_top forward
            if (p == free_end) {
                free_end = prev(free_end);
                return;
            }

            // if p is free_top, we have to update free_top
            if (p == free_top) free_top = next(free_top);
            remove(p);
            insert_after(p, free_end);
        }

        // untracked slots are considered always full
        void detrack(size_t p) {
            if (p == npos) return;
            if (p == free_top) {
                if (free_top == free_end) free_end = npos;
                free_top = next(free_top);
            } else if (p == free_end) {
                free_end = prev(free_end);
            }
            remove(p);
        }
};