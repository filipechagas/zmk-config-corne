#!/usr/bin/env bash
set -euo pipefail

# Make Git trust bind-mounted repos inside the container
git config --global --add safe.directory /zmk-config/zephyr || true
git config --global --add safe.directory /zmk-config/zmk || true
git config --global --add safe.directory /zmk-config || true

# Initialize and update west (idempotent init)
if [ ! -d .west ]; then
  west init -l config
fi
west update

# Optional: show manifest status for debugging
west -vv status || true

# Ensure CMake can find Zephyr; tolerate unset var when -u is enabled
export CMAKE_PREFIX_PATH="/zmk-config/zephyr:${CMAKE_PREFIX_PATH:-}"

# The repo root is a Zephyr module (zephyr/module.yml): the status GATT
# service in src/. Its Kconfig limits it to the split central (left half).

# Build left side
west build -d /build/left -p -b nice_nano_v2 \
  -s /zmk-config/zmk/app \
  -- -DSHIELD=corne_left \
     -DZMK_CONFIG=/zmk-config/config \
     -DZMK_EXTRA_MODULES=/zmk-config

# Build right side
west build -d /build/right -p -b nice_nano_v2 \
  -s /zmk-config/zmk/app \
  -- -DSHIELD=corne_right \
     -DZMK_CONFIG=/zmk-config/config \
     -DZMK_EXTRA_MODULES=/zmk-config

# Build settings reset
west build -d /build/settings_reset -p -b nice_nano_v2 \
  -s /zmk-config/zmk/app \
  -- -DSHIELD=settings_reset \
     -DZMK_CONFIG=/zmk-config/config \
     -DZMK_EXTRA_MODULES=/zmk-config

# Copy firmware files to output directory
mkdir -p /output
cp /build/left/zephyr/zmk.uf2 /output/corne_left-nice_nano_v2-zmk.uf2
cp /build/right/zephyr/zmk.uf2 /output/corne_right-nice_nano_v2-zmk.uf2
cp /build/settings_reset/zephyr/zmk.uf2 /output/settings_reset.uf2
