#!/usr/bin/env bash
# Native Linux validation of shared modules and UI, not the Windows deliverable.
set -euo pipefail
if [[ $# != 2 ]]; then
    printf 'Usage: %s /path/to/SDL/include /path/to/SDL/shared-library-directory\n' "$0" >&2
    exit 2
fi
root=$(cd -- "$(dirname -- "$0")/../.." && pwd)
headers=$(cd -- "$1" && pwd)
lib=$(cd -- "$2" && pwd)
imgui="$root/third_party/imgui"
work=$(mktemp -d "${TMPDIR:-/tmp}/campuslink test.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
version=$(<"$root/VERSION")
expect_exit() {
    local want=$1 output rc=0
    shift
    output=$(timeout 12s "$@" 2>&1) || rc=$?
    printf '%s\n' "$output"
    if [[ $rc != "$want" ]]; then printf 'FAIL: expected exit %d, got %d\n' "$want" "$rc" >&2; return 1; fi
}
cd "$root"
core=(src/simulation/lab.cpp src/network/diagnostics.cpp src/network/dns.cpp src/performance/score.cpp)
vendor=(imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp backends/imgui_impl_sdl3.cpp backends/imgui_impl_sdlgpu3.cpp backends/imgui_impl_sdlrenderer3.cpp)
for config in debug release; do
    flags=(-O0 -g)
    if [[ $config == release ]]; then flags=(-O2 -DNDEBUG); fi
    echo "=== $config Linux host ==="
    common=(-std=c++20 -pthread "${flags[@]}" "-DCAMPUSLINK_VERSION=$version" -Iinclude -isystem "$headers" -isystem "$imgui" -isystem "$imgui/backends")
    objs=()
    for source in "${core[@]}" src/main.cpp src/platform/unavailable.cpp src/ui/desktop.cpp tests/unit/core.cpp; do
        obj="$work/${source//\//_}.o"
        "${CXX:-g++}" "${common[@]}" -Wall -Wextra -Wpedantic -Werror -c "$source" -o "$obj"
        objs+=("$obj")
    done
    "${CXX:-g++}" -pthread "${objs[@]:0:4}" "${objs[-1]}" -o "$work/core-tests"
    expect_exit 0 "$work/core-tests"
    unset 'objs[-1]'
    for source in "${vendor[@]}"; do
        obj="$work/${source//\//_}.o"
        "${CXX:-g++}" "${common[@]}" -c "$imgui/$source" -o "$obj"
        objs+=("$obj")
    done
    "${CXX:-g++}" -pthread "${objs[@]}" -L"$lib" -Wl,-rpath,"$lib" -lSDL3 -o "$work/CampusLink"
    expect_exit 0 env SDL_VIDEO_DRIVER=nonexistent "$work/CampusLink" --self-test
    expect_exit 0 env SDL_VIDEO_DRIVER=nonexistent "$work/CampusLink" --ui-smoke
    expect_exit 0 "$work/CampusLink" --profile
    expect_exit 2 "$work/CampusLink" --bad-argument
    expect_exit 1 env SDL_VIDEO_DRIVER=nonexistent "$work/CampusLink"
done
