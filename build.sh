#!/bin/bash

# Initialize and update west
west init -l config
west update

# Export CMAKE_PREFIX_PATH
export "CMAKE_PREFIX_PATH=/zmk-config/zephyr:$CMAKE_PREFIX_PATH"

# Build left side
west build -d /build/left -p -b "nice_nano_v2" \
  -s /zmk-config/zmk/app \
  -- -DSHIELD="corne_left" \
  -DZMK_CONFIG="/zmk-config/config"

# Build right side
west build -d /build/right -p -b "nice_nano_v2" \
  -s /zmk-config/zmk/app \
  -- -DSHIELD="corne_right" \
  -DZMK_CONFIG="/zmk-config/config"

# Build settings reset
west build -d /build/settings_reset -p -b "nice_nano_v2" \
  -s /zmk-config/zmk/app \
  -- -DSHIELD="settings_reset" \
  -DZMK_CONFIG="/zmk-config/config"

# Copy firmware files to output directory
mkdir -p /output
cp /build/left/zephyr/zmk.uf2 /output/corne_left-nice_nano_v2-zmk.uf2
cp /build/right/zephyr/zmk.uf2 /output/corne_right-nice_nano_v2-zmk.uf2
cp /build/settings_reset/zephyr/zmk.uf2 /output/settings_reset.uf2
