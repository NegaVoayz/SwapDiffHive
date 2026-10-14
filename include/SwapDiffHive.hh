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
            if (data_id == data.size()) {
                data.emplace_back(std::move(payload), diff_id);
                return to_diff(Diff::Unary{diff_id, data_id});
            }

            Id temp_id = temp_link.get_empty_slot();
            if (temp_id == temp.size()) temp.emplace_back(std::move(payload), 0);
            else temp[temp_id] = Slot{std::move(payload), 0};

            return to_diff(Diff::Binary{diff_id, data_id, temp_id});
        }

        void apply_diff(const Diff& d) {
            apply(d,
                std::bind_front(&SwapDiffHive::apply_unary, this),
                std::bind_front(&SwapDiffHive::apply_binary, this));
        }

        // last index satisfies history[i].common.diff_id <= diff_id
        Id target_index(Id diff_id) const {
            if (history.empty()) return npos;
            if (history.back().common.diff_id == diff_id) 
                return history.size() - 1;
            auto begin = history.begin();
            auto end   = history.end();

            auto it = std::upper_bound(
                begin, end, diff_id,
                [](Id value, const Diff& d) {
                    return value < d.common.diff_id;
                });

            if (it == begin) return npos;
            return static_cast<Id>(std::distance(begin, it)) - 1;
        }

    public: 
        
        void remove(Id diff_id, Id data_id) {
            history.push_back(
                prepare_remove(diff_id, data_id));
        }

        Id insert(Id diff_id, T&& payload) {
            Diff diff = prepare_insert(diff_id, std::move(payload));
            history.push_back(diff);
            return diff.common.data_id;
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
            if (current_pos != history.size()-1) return false;
            for (Id i = 0; i < temp.size(); ++i)
                if (temp[i].book && temp[i].book < trace_limit) {
                    temp[i].book = 0;
                    temp_link.free(i);
                }
            return true;
        }
};
