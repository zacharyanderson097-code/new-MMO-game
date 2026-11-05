#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
compiler=""
for candidate in c++ g++ clang++; do
    if command -v "$candidate" >/dev/null 2>&1; then
        compiler="$candidate"
        break
    fi
done
if [[ -z "$compiler" ]]; then
    printf '%s\n' 'A C++17 compiler (c++, g++, or clang++) is required to run the rules tests.' >&2
    exit 1
fi

build_dir="$(mktemp -d "${TMPDIR:-/tmp}/emberfall-rules.XXXXXX")"
trap 'rm -rf -- "$build_dir"' EXIT
"$compiler" -std=c++17 -Wall -Wextra -Werror -pedantic -UNDEBUG \
    -I "$project_dir/Source/Emberfall/Public" \
    "$project_dir/Tests/rules_tests.cpp" -o "$build_dir/rules_tests"
"$build_dir/rules_tests"
