#pragma once
#include <algorithm>
#include <exception>
#include <functional>
#include <utility>
#include <vector>
#include "Def.hh"
#include "Diff.hh"
#include "FreeChain.hh"

template<typename T>
class SwapDiffHive {
    private:
        struct Slot {
            T data;
            Id book;
        };
        
        FreeChain data_link{};
        std::vector<Slot> data{};
        
        FreeChain temp_link{};
        std::vector<Slot> temp{};

        Id current_pos = npos;
        std::vector<Diff> history{};
        
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

        // last index in history with diff_id' <= diff_id; the unsigned
        // wraparound yields npos when diff_id is older than history[0]
        Id target_index(Id diff_id) const {
            const auto it = std::upper_bound(
                history.begin(), history.end(), diff_id,
                [](Id value, const Diff& d) {
                    return value < d.common.diff_id;
                });
            return static_cast<Id>(std::distance(history.begin(), it)) - 1;
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
            Id target = target_index(diff_id);

            // seek backwards: history[current_pos], ..., history[target + 1]
            while (current_pos != target && current_pos != npos) {
                apply_diff(history[current_pos]);
                --current_pos;
            }

            // seek forwards: history[0], history[1], ..., history[target]
            while (current_pos != target) {
                ++current_pos;
                apply_diff(history[current_pos]);
            }
        }

        bool compact_slots(size_t trace_limit) {
            if (current_pos + 1 != history.size()) return false;
            for (Id i = 0; i < temp.size(); ++i)
                if (temp[i].book && temp[i].book < trace_limit) {
                    temp[i].book = 0;
                    temp_link.free(i);
                }
            return true;
        }

#ifdef SWAP_DIFF_HIVE_TEST
        // read-only views of the stored slots, for tests/ only
        const std::vector<Slot>& test_data() const { return data; }
        const std::vector<Slot>& test_temp() const { return temp; }
        const std::vector<Diff>& test_history() const { return history; }
        Id test_position() const { return current_pos; }
#endif
};
