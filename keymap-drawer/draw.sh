#!/bin/sh

rm -f keymap.svg definition.yaml
keymap -c config.yaml parse -z ../config/corne.keymap >definition.yaml
cat combos.yml >>definition.yaml
keymap -c config.yaml draw definition.yaml >keymap.svg
