# Corne ZMK Firmware

![CleanShot 2025-05-05 at 15 15 37@2x](https://github.com/user-attachments/assets/8c125c85-62d3-428f-a39c-4676667129b3)

## Layers
![my_keymap (1)](https://github.com/user-attachments/assets/e2628240-24f9-4b01-b779-4edb9239b2a0)

## Timewless home row mod

I'm copying the implementation of timeless homerow mods from https://github.com/urob/zmk-config/tree/main?tab=readme-ov-file#timeless-homerow-mods

## What am I looking at?

This is the [ZMK](https://zmk.dev/docs) firmware
for the Corne family of split ergonomic keyboards.

It contains keymap definition files for three boards in [./config](./config):

Pushing changes will build all the keyboards. You need to be signed in to a GitHub account to push changes and build the firmware. To not waste build time, comment out the keyboards in [./build.yaml](./build.yaml) that you do not have.

To build the firmware:

- Fork this repo on GitHub
- Clone your fork locally
- Trigger a build:
  - Make a trivial change to ./build.yaml (or any non \*.md file)
  - Push that change
- Look in the [Actions](https://github.com/mmccoyd/zmk-config/actions) tab
  for the build triggered by that change.
- Wait for the build to finish
- Click on the build link next to the green checkbox
- Download the artifact file with the firmware
- See [Installing The Firmware](https://zmk.dev/docs/user-setup#installing-the-firmware)
  for more details from there.

_Once_ your board works with the default firmware,
you can modify the keymap.
Your copy of the default Corne keymaps are in:

- [./config/corne.keymap](./config/hillside52.keymap)

Modify those as needed. Pushing the change will trigger a build as above.

If you want to enable features, modify ./config/corne.conf file.
