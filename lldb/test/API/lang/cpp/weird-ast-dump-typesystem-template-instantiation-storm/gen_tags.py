"""
Code generator invoked by the Makefile (NOT part of the test's Python
harness) to produce the two "Tag<N> instantiation storm" headers that
main.cpp and plugin.cpp include.

Usage: python3 gen_tags.py <main|plugin> <N_MAX> <output_file>

For side == "main":
  - Defines the class template 'Tag' and its 'Tag<0>' base-case explicit
    specialization.
  - Force-instantiates 'Tag<N>' for every EVEN N in [1, N_MAX] as a global
    variable, so the implicit-instantiation chain 'Tag<N> -> Tag<N-1> ->
    ... -> Tag<0>' (through the 'prev' member) ends up in main.cpp's debug
    info for every even N (and, transitively, quite a few odd N as well,
    as the 'prev' pointee of an even Tag).

For side == "plugin":
  - Defines the same primary template 'Tag' and 'Tag<0>' base case.
  - Additionally provides an EXPLICIT specialization 'Tag<N>' for every ODD
    N in [1, N_MAX] with the 'prev'/'arr' fields swapped relative to the
    primary template's (implicit, main.cpp-side) member order. This is a
    genuine ODR violation: the same specialization 'Tag<N>' (for odd N) has
    two different definitions depending on which module's debug info you
    look at.
  - Force-instantiates 'Tag<N>' for every ODD N in [1, N_MAX] as a global
    variable, using its own conflicting explicit specializations.

Splitting the instantiations this way means that, by the time both
main.cpp's and plugin.cpp's globals are alive, the scratch/per-module Clang
ASTContexts have to deal with roughly N_MAX chained 'Tag<K>' specializations
(1..N_MAX, terminating at 'Tag<0>'), about half of which (every odd K) are
genuinely ODR-conflicting between the two modules.
"""

import sys


def main():
    if len(sys.argv) != 4:
        sys.exit("usage: gen_tags.py <main|plugin> <N_MAX> <output_file>")

    side, n_max_str, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
    n_max = int(n_max_str)

    lines = [
        "// GENERATED FILE -- produced by gen_tags.py, do not edit by hand.",
        "#pragma once",
        "",
        "template <int N> struct Tag {",
        "  int arr[N];",
        "  Tag<N - 1> *prev;",
        "};",
        "",
        "template <> struct Tag<0> {",
        "  Tag<0> *prev;",
        "  int arr[1];",
        "};",
        "",
    ]

    if side == "plugin":
        # Conflicting explicit specializations for every odd N: field order
        # is swapped ('prev' before 'arr') relative to the primary
        # template's implicit instantiations that main.cpp relies on.
        for n in range(1, n_max + 1):
            if n % 2 == 1:
                lines.append(f"template <> struct Tag<{n}> {{")
                lines.append(f"  Tag<{n} - 1> *prev;")
                lines.append(f"  int arr[{n}];")
                lines.append("};")
        lines.append("")

    parity = 0 if side == "main" else 1
    prefix = "main" if side == "main" else "plugin"
    lines.append(f"namespace {prefix}_storm {{")
    for n in range(1, n_max + 1):
        if n % 2 == parity:
            lines.append(f"Tag<{n}> g_{prefix}_tag_{n};")
    lines.append("} // namespace")
    lines.append("")

    with open(out_path, "w") as f:
        f.write("\n".join(lines))


if __name__ == "__main__":
    main()
