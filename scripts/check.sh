#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

warn()
{
    if [[ -t 2 && -z "${NO_COLOR:-}" ]]; then
        printf '\033[38;5;208mwarning:\033[0m %s\n' "$*" >&2
    else
        printf 'warning: %s\n' "$*" >&2
    fi
}

run_step()
{
    local title="${1^^}"
    local width=80
    local padding=$((width - ${#title} - 2))
    local left_width=$((padding / 2))
    local right_width=$((padding - left_width))
    local left
    local right
    local status
    shift

    printf -v left '%*s' "${left_width}" ''
    printf -v right '%*s' "${right_width}" ''
    printf '\n%s %s %s\n' "${left// /▓}" "${title}" "${right// /▓}"
    if "$@"; then
        return
    else
        status=$?
    fi

    if ((status == 3)); then
        return
    fi

    return "${status}"
}

check_cpp_file()
{
    local file="$1"

    clang-format "${file}" | diff --color=always -u --label "${file}" --label "${file} (clang-format)" "${file}" -
}

check_cmake_file()
{
    local file="$1"

    cmake-format "${file}" | diff --color=always -u --label "${file}" --label "${file} (cmake-format)" "${file}" -
}

check_cpp_format()
{
    local option="${1:-}"
    local failed=0
    local -a files

    mapfile -d '' files < <(
        find include src tests \
            \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) \
            -print0 | sort -z
    )

    if [[ "${option}" == "--fix" ]]; then
        clang-format -i "${files[@]}"
        return
    fi

    for file in "${files[@]}"; do
        if ! check_cpp_file "${file}"; then
            failed=1
        fi
    done

    return "${failed}"
}

check_cmake_format()
{
    local option="${1:-}"

    if ! command -v cmake-format >/dev/null; then
        warn "cmake-format not found; skipping CMake formatting"
        return 3
    fi

    local failed=0
    local -a files

    mapfile -d '' files < <(
        find . \
            \( -path './build' -o -path './.git' -o -path './.idea' -o -path './task' \) -prune -o \
            \( -name 'CMakeLists.txt' -o -name '*.cmake' \) \
            -print0 | sort -z
    )

    if [[ "${option}" == "--fix" ]]; then
        for file in "${files[@]}"; do
            cmake-format -i "${file}"
        done
        return
    fi

    for file in "${files[@]}"; do
        if ! check_cmake_file "${file}"; then
            failed=1
        fi
    done

    return "${failed}"
}

check_shell_format()
{
    local option="${1:-}"

    if ! command -v shfmt >/dev/null; then
        warn "shfmt not found; skipping shell formatting"
        return 3
    fi

    local -a files
    mapfile -d '' files < <(find scripts -name '*.sh' -print0 | sort -z)

    if ((${#files[@]} > 0)); then
        if [[ "${option}" == "--fix" ]]; then
            shfmt -w "${files[@]}"
        else
            shfmt -d "${files[@]}"
        fi
    fi
}

check_format()
{
    local option="${1:-}"
    local failed=0

    run_step "C++ format" check_cpp_format "${option}" || failed=1
    run_step "CMake format" check_cmake_format "${option}" || failed=1
    run_step "Shell format" check_shell_format "${option}" || failed=1

    return "${failed}"
}

build_project()
{
    cmake --preset sanitize
    cmake --build build/sanitize
}

run_tests()
{
    ctest --preset sanitize
}

run_tidy()
{
    local -a files
    mapfile -d '' files < <(find src tests -name '*.cpp' -print0 | sort -z)

    if ((${#files[@]} > 0)); then
        clang-tidy -quiet -p build/sanitize "${files[@]}"
    fi
}

run_all()
{
    check_format "${1:-}"
    run_step "Build" build_project
    run_step "clang-tidy" run_tidy
    run_step "Tests" run_tests
}

usage()
{
    echo "usage: $0 [all|format|format-cpp|format-cmake|format-shell] [--fix]" >&2
    echo "       $0 [build|test|tidy]" >&2
}

if (($# > 2)); then
    usage
    exit 2
fi

command="${1:-all}"
option="${2:-}"

if [[ -n "${option}" && "${option}" != "--fix" ]]; then
    usage
    exit 2
fi

case "${command}" in
all) run_all "${option}" ;;
format) check_format "${option}" ;;
format-cpp) run_step "C++ format" check_cpp_format "${option}" ;;
format-cmake) run_step "CMake format" check_cmake_format "${option}" ;;
format-shell) run_step "Shell format" check_shell_format "${option}" ;;
build | test | tidy)
    if [[ -n "${option}" ]]; then
        usage
        exit 2
    fi
    case "${command}" in
    build) run_step "Build" build_project ;;
    test) run_step "Tests" run_tests ;;
    tidy) run_step "clang-tidy" run_tidy ;;
    esac
    ;;
*)
    usage
    exit 2
    ;;
esac
