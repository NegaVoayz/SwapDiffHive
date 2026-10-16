#pragma once
#include <algorithm>
#include <deque>
#include <exception>
#include <functional>
#include <iterator>
#include <stdexcept>
#include <utility>
#include "Def.hh"
#include "Diff.hh"
#include "FreeChain.hh"

template<typename T, template<typename> typename Container = std::deque>
class SwapDiffHive {
    private:
        struct Slot {
            T data;
            Id book;
        };
        
        FreeChain data_link{};
        Container<Slot> data{};
        
        FreeChain temp_link{};
        Container<Slot> temp{};

        Id current_pos = npos;
        Container<Diff> history{};

        // a transaction groups the ops that follow under one diff id; they are
        // applied as they come, so commit() only closes the group while
        // rollback() returns to the block that preceded it
        bool transaction_open = false;
        bool transaction_wrote = false;
        Id transaction_id = 0;

        void require_open() const {
            if (!transaction_open) throw std::logic_error("SwapDiffHive: no transaction is open");
        }
        void require_closed() const {
            if (transaction_open) throw std::logic_error("SwapDiffHive: a transaction is already open");
        }

        void apply_unary(const Diff::Unary& unary) {
            data[unary.data_id].book ^= unary.diff_id;
            data_link.flip(unary.data_id);
        }
        void apply_replace(const Diff::Replace& replace) {
            std::swap(data[replace.data_id], temp[replace.temp_id]);
            data_link.flip(replace.data_id);
            temp_link.flip(replace.temp_id);
        }
        void apply_swap(const Diff::Swap& swap) {
            if (swap.data_a == swap.data_b) return;   // a move based swap of one slot with itself is not safe
            // both slots are in use, so both books are 0 and the free lists are
            // untouched; swapping a slot that was removed is undefined
            std::swap(data[swap.data_a], data[swap.data_b]);
        }

        Diff prepare_remove(Id diff_id, Id data_id) {
            if (data_id >= data.size()) std::terminate();
            return to_diff(Diff::Unary{diff_id, data_id});
        }
        Diff prepare_swap(Id diff_id, Id data_a, Id data_b) {
            if (data_a >= data.size() || data_b >= data.size()) std::terminate();
            return to_diff(Diff::Swap{diff_id, data_a, data_b});
        }
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

            return to_diff(Diff::Replace{diff_id, data_id, temp_id});
        }

        void apply_diff(const Diff& d) {
            apply(d,
                std::bind_front(&SwapDiffHive::apply_unary, this),
                std::bind_front(&SwapDiffHive::apply_replace, this),
                std::bind_front(&SwapDiffHive::apply_swap, this));
        }

        // a new Diff always lands at current_pos + 1 and is committed right
        // away: on the head that is a push_back, otherwise it overwrites the
        // deprecated tail, then applying it advances current_pos by 1
        void commit(Diff diff) {
            history.resize(current_pos + 1);
            history.push_back(diff);
            ++current_pos;
            apply_diff(history[current_pos]);
        }

        void remove_with(Id diff_id, Id data_id) {
            commit(prepare_remove(diff_id, data_id));
        }
        Id insert_with(Id diff_id, T&& payload) {
            Diff diff = prepare_insert(diff_id, std::move(payload));
            const Id data_id = diff.common.data_id;
            commit(diff);
            return data_id;
        }

    public: 
        
        // one op recorded under the id it is given: the form to use outside a
        // transaction
        void remove(Id diff_id, Id data_id) {
            require_closed();
            remove_with(diff_id, data_id);
        }

        Id insert(Id diff_id, T&& payload) {
            require_closed();
            return insert_with(diff_id, std::move(payload));
        }

        // inside a transaction the id is the one begin_transaction() was given
        void remove(Id data_id) {
            require_open();
            transaction_wrote = true;
            remove_with(transaction_id, data_id);
        }

        Id insert(T&& payload) {
            require_open();
            transaction_wrote = true;
            return insert_with(transaction_id, std::move(payload));
        }

        // exchange two slots: the payloads and the removal books that belong to
        // them travel together, so the live set and the recoverable payloads
        // follow them across the two ids
        void swap(Id diff_id, Id data_a, Id data_b) {
            require_closed();
            commit(prepare_swap(diff_id, data_a, data_b));
        }

        void swap(Id data_a, Id data_b) {
            require_open();
            transaction_wrote = true;
            commit(prepare_swap(transaction_id, data_a, data_b));
        }

        // group the ops that follow under one diff id so that they form a single
        // block: undo() and redo() move the group as a unit, and rollback() lands
        // exactly where the group started
        void begin_transaction(Id diff_id) {
            require_closed();
            const Id previous = current_pos == npos ? 0 : history[current_pos].common.diff_id;
            if (diff_id <= previous) {
                throw std::logic_error("SwapDiffHive: the transaction id must be a new, larger id");
            }
            transaction_id = diff_id;
            transaction_wrote = false;
            transaction_open = true;
        }

        // the ops are already applied, so this only closes the group
        void commit() {
            require_open();
            transaction_open = false;
        }

        // undo the group, drop it, and close it: nothing of it is left for redo()
        void rollback() {
            require_open();
            transaction_open = false;
            if (!transaction_wrote) return;   // an empty transaction takes nothing back
            undo();
            history.resize(current_pos + 1);
        }

        // raw slot access: the id has to be a slot of this hive, and a slot that
        // is not in use still holds the payload a rollback would restore
        const T& operator[](Id data_id) const { return data[data_id].data; }

        // the same, but only for a slot that is in use
        const T& at(Id data_id) const {
            if (data_id >= data.size() || data[data_id].book != 0) {
                throw std::out_of_range("SwapDiffHive::at: no element at this slot");
            }
            return data[data_id].data;
        }

        void seek(Id diff_id) {
            require_closed();
            // diff ids start at 1 (a 0 would make the book xor a no-op), so 0
            // asks for the state before everything that is left
            while (current_pos != npos && history[current_pos].common.diff_id > diff_id) {
                apply_diff(history[current_pos]);
                --current_pos;
            }

            // then re-apply up to the end of the target block
            while (current_pos + 1 < history.size() && history[current_pos + 1].common.diff_id <= diff_id) {
                ++current_pos;
                apply_diff(history[current_pos]);
            }
        }

        // drops everything that can no longer be reached: the history below the
        // earliest traceable diff, and the temp records no diff left needs
        bool compact(size_t trace_limit) {
            require_closed();
            if (current_pos + 1 != history.size()) return false;

            // history is ordered by diff_id, so the out-of-limit entries are a
            // prefix and current_pos shifts by exactly that much
            const auto first_kept = std::lower_bound(
                history.begin(), history.end(), static_cast<Id>(trace_limit),
                [](const Diff& d, Id limit) { return d.common.diff_id < limit; });

            // a remaining diff still needs the temporary record it parked, no
            // matter how old the book on it is
            Container<char> needed(temp.size(), 0);
            for (auto it = first_kept; it != history.end(); ++it)
                if (it->kind == Diff::Kind::Replace) needed[it->replace.temp_id] = 1;

            for (Id i = 0; i < temp.size(); ++i)
                if (!needed[i] && temp[i].book && temp[i].book < trace_limit) {
                    temp[i].book = 0;
                    temp_link.free(i);
                }

            current_pos -= static_cast<Id>(std::distance(history.begin(), first_kept));
            history.erase(history.begin(), first_kept);
            return true;
        }

        // forget the history and the records it could roll back to: the state
        // stays as it is and becomes the new baseline
        void clear_history() {
            require_closed();
            history.clear();
            current_pos = npos;
            temp.clear();
            temp_link.clear();
        }

        // forget everything, as if the hive had just been constructed
        void clear() {
            require_closed();
            clear_history();
            data.clear();
            data_link.clear();
        }

        // the two steps used instead of seek: exactly one block of equal diff
        // ids back or forward, stepped over the history itself so no fabricated
        // diff id is ever passed around
        void undo() {
            require_closed();
            if (current_pos == npos) return;                       // nothing applied
            const Id block = history[current_pos].common.diff_id;
            while (current_pos != npos && history[current_pos].common.diff_id == block) {
                apply_diff(history[current_pos]);
                --current_pos;
            }
        }

        void redo() {
            require_closed();
            if (current_pos + 1 >= history.size()) return;         // nothing queued
            const Id block = history[current_pos + 1].common.diff_id;
            while (current_pos + 1 < history.size() && history[current_pos + 1].common.diff_id == block) {
                ++current_pos;
                apply_diff(history[current_pos]);
            }
        }

#ifdef SWAP_DIFF_HIVE_TEST
        // read-only views of the stored slots, for tests/ only
        const Container<Slot>& test_data() const { return data; }
        const Container<Slot>& test_temp() const { return temp; }
        const Container<Diff>& test_history() const { return history; }
        Id test_position() const { return current_pos; }
#endif
};
