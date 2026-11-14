#!/bin/sh
# SPDX-License-Identifier: MPL-2.0 (from AMB-1's build.sh)
# Copyright (C) 2026 Charles Vestal
# Build HYP-1 with fm1-base (base/, a git submodule).
#   ./build.sh [--release X.Y] [--host | --web]
#   JIELI_TOOLCHAIN  JieLi Linux toolchain (default: ~/.jieli/toolchain)
#   AC79_SDK         JieLi AC79 SDK checkout (default: ~/fw-AC79_AIoT_SDK)
#   PYTHON           a Python with Pillow (and libraqm, for the fonts)
set -e
cd "$(dirname "$0")"
export JIELI_TOOLCHAIN="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
PY="${PYTHON:-python3}"
[ -f base/tools/build.py ] || { echo "build.sh: no fm1-base in base/ (git submodule update --init --recursive)"; exit 1; }
"$PY" -c 'import PIL' 2>/dev/null || { echo "build.sh: $PY has no Pillow (pip3 install Pillow)"; exit 1; }
# the screen's face: Chakra Petch, from assets/fonts (fm1-base's gen_font.py has the set)
export X0X_FONTSET="${X0X_FONTSET:-chakra}"
export X0X_FONT_DIRS="${X0X_FONT_DIRS:-$PWD/assets/fonts}"
exec "$PY" base/tools/build.py app "$@"
