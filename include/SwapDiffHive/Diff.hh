#pragma once
#include "Def.hh"
#include <functional>
#include <utility>

struct Diff {
    struct Common {
        Id diff_id;
        Id data_id;
    };
    struct Unary {
        Id diff_id;
        Id data_id;
    };
    struct Replace {
        Id diff_id;
        Id data_id;
        Id temp_id;
    };
    struct Swap {
        Id diff_id;
        Id data_a;
        Id data_b;
    };

    enum class Kind : std::uint8_t { Unary, Replace, Swap } kind;
    union {
        Common common;
        Unary unary;
        Replace replace;
        Swap swap;
    };
};

inline Diff to_diff(const Diff::Unary& diff_unary) {
    Diff result;
    result.kind = Diff::Kind::Unary;
    result.unary = diff_unary;
    return result;
}
inline Diff to_diff(const Diff::Replace& diff_replace) {
    Diff result;
    result.kind = Diff::Kind::Replace;
    result.replace = diff_replace;
    return result;
}
inline Diff to_diff(const Diff::Swap& diff_swap) {
    Diff result;
    result.kind = Diff::Kind::Swap;
    result.swap = diff_swap;
    return result;
}

template<typename FuncUnary, typename FuncReplace, typename FuncSwap>
inline void apply(const Diff& diff, FuncUnary func_unary, FuncReplace func_replace, FuncSwap func_swap) {
    switch (diff.kind) {
        case Diff::Kind::Unary:
            std::invoke(func_unary,diff.unary);
            return;
        case Diff::Kind::Replace:
            std::invoke(func_replace,diff.replace);
            return;
        case Diff::Kind::Swap:
            std::invoke(func_swap,diff.swap);
            return;
        default:
            std::unreachable();
    }
}
