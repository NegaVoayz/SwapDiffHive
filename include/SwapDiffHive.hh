#pragma once
#include <algorithm>
#include <deque>
#include <exception>
#include <functional>
#include <iterator>
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
        
        void apply_unary(const Diff::Unary& unary) {
            data[unary.data_id].book ^= unary.diff_id;
            data_link.flip(unary.data_id);
        }
        void apply_binary(const Diff::Binary& binary) {
            std::swap(data[binary.data_id], temp[binary.temp_id]);
            data_link.flip(binary.data_id);
            temp_link.flip(binary.temp_id);
        }

        Diff prepare_remove(Id diff_id, Id data_id) {
            if (data_id >= data.size()) std::terminate();
            return to_diff(Diff::Unary{diff_id, data_id});
        }
        Diff prepare_insert(Id diff_id, T&& payload) {
            Id data_id = data_link.get_empty_slot();
            // append fast path: a brand new slot has no old payload to park, so
            // flipping the book is cheaper than burning a temp slot
            if (data_id == data.size()) {
                data.emplace_back(std::move(payload), diff_id);
                return to_diff(Diff::Unary{diff_id, data_id});
            }

            Id temp_id = temp_link.get_empty_slot();
            temp.resize(std::max(temp.size(), temp_id + 1));
            temp[temp_id] = Slot{std::move(payload), 0};

            return to_diff(Diff::Binary{diff_id, data_id, temp_id});
        }

        void apply_diff(const Diff& d) {
            apply(d,
                std::bind_front(&SwapDiffHive::apply_unary, this),
                std::bind_front(&SwapDiffHive::apply_binary, this));
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

    public: 
        
        void remove(Id diff_id, Id data_id) {
            commit(prepare_remove(diff_id, data_id));
        }

        Id insert(Id diff_id, T&& payload) {
            Diff diff = prepare_insert(diff_id, std::move(payload));
            const Id data_id = diff.common.data_id;
            commit(diff);
            return data_id;
        }

        void seek(Id diff_id) {
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

        // the two steps used instead of seek: exactly one block of equal diff
        // ids back or forward, stepped over the history itself so no fabricated
        // diff id is ever passed around
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

#ifdef SWAP_DIFF_HIVE_TEST
        // read-only views of the stored slots, for tests/ only
        const Container<Slot>& test_data() const { return data; }
        const Container<Slot>& test_temp() const { return temp; }
        const Container<Diff>& test_history() const { return history; }
        Id test_position() const { return current_pos; }
#endif
};
