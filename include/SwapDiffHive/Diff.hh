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
    struct Binary {
        Id diff_id;
        Id data_id;
        Id temp_id;
    };

    enum class Kind : std::uint8_t { Unary, Binary } kind;
    union {
        Common common;
        Unary unary;
        Binary binary;
    };
};

inline Diff to_diff(const Diff::Unary& diff_unary) {
    Diff result;
    result.kind = Diff::Kind::Unary;
    result.unary = diff_unary;
    return result;
}
inline Diff to_diff(const Diff::Binary& diff_binary) {
    Diff result;
    result.kind = Diff::Kind::Binary;
    result.binary = diff_binary;
    return result;
}

template<typename FuncUnary, typename FuncBinary>
inline void apply(const Diff& diff, FuncUnary func_unary, FuncBinary func_binary) {
    switch (diff.kind) {
        case Diff::Kind::Unary:
            std::invoke(func_unary,diff.unary);
            return;
        case Diff::Kind::Binary:
            std::invoke(func_binary,diff.binary);
            return;
        default:
            std::unreachable();
    }
}
