#!/usr/bin/env bash
set -euo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly ELF="build/Debug/uart03_dma_idle.elf"

cd "$SCRIPT_DIR"

if [[ ! -f "$ELF" ]]; then
    echo "Error: $ELF not found."
    echo "Run ./build.sh all first."
    exit 1
fi

echo "=== Flash STM32F401RE ==="

openocd \
    -f interface/stlink.cfg \
    -f target/stm32f4x.cfg \
    -c "program $ELF verify reset exit"

echo "=== Flash Complete ==="
