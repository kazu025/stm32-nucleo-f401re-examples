
#!/usr/bin/env bash

set -e

ELF="build/Debug/uart01_irq.elf"

if [ ! -f "$ELF" ]; then
    echo "Error: $ELF not found."
    echo "Run ./build.sh first."
    exit 1
fi

echo "=== Flash STM32F401RE ==="

openocd \
    -f interface/stlink.cfg \
    -f target/stm32f4x.cfg \
    -c "program $ELF verify reset exit"

echo "=== Flash Complete ==="
