#!/usr/bin/env bash
set -euo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly PRESET="Debug"

cd "$SCRIPT_DIR"

usage() {
    cat <<'EOF'
Usage: ./build.sh [command]

Commands:
  all     Configure and build the Debug firmware (default)
  clean   Remove files produced by the Debug build
  flash   Build and write the firmware with OpenOCD
  halt    Connect with OpenOCD and halt the MCU
  run     Reset and run the firmware already in Flash
  resume  Resume execution from the current position
  help    Show this help
EOF
}

build_all() {
    echo "=== CMake Configuration ==="
    cmake --preset "$PRESET"

    echo "=== Build ==="
    cmake --build --preset "$PRESET"

    echo "=== Build Complete ==="
}

clean_build() {
    if [[ ! -f "build/$PRESET/build.ninja" ]]; then
        echo "=== Already Clean: build/$PRESET does not exist ==="
        return
    fi

    echo "=== Clean ==="
    cmake --build --preset "$PRESET" --target clean
    echo "=== Clean Complete ==="
}

flash_firmware() {
    build_all
    ./flash.sh
}

halt_target() {
    echo "=== Halt STM32F401RE ==="
    openocd \
        -f interface/stlink.cfg \
        -f target/stm32f4x.cfg \
        -c "init" \
        -c "halt" \
        -c "shutdown"
    echo "=== Halt Complete ==="
}

run_target() {
    echo "=== Reset and Run STM32F401RE ==="
    openocd \
        -f interface/stlink.cfg \
        -f target/stm32f4x.cfg \
        -c "init" \
        -c "reset run" \
        -c "shutdown"
    echo "=== Run Complete ==="
}

resume_target() {
    echo "=== Resume STM32F401RE ==="
    openocd \
        -f interface/stlink.cfg \
        -f target/stm32f4x.cfg \
        -c "init" \
        -c "resume" \
        -c "shutdown"
    echo "=== Resume Complete ==="
}

if (( $# > 1 )); then
    usage >&2
    exit 2
fi

case "${1:-all}" in
    all)
        build_all
        ;;
    clean)
        clean_build
        ;;
    flash)
        flash_firmware
        ;;
    halt)
        halt_target
        ;;
    run)
        run_target
        ;;
    resume)
        resume_target
        ;;
    help|-h|--help)
        usage
        ;;
    *)
        echo "Error: unknown command: $1" >&2
        usage >&2
        exit 2
        ;;
esac
