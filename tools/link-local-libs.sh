#!/bin/sh
# Points lib/ at the M5Stack library trees that already build for this board.
#
# These four are not in lib_deps: Library Dependency Finder resolves a DECLARED
# dependency (M5Unified wants M5GFX >= 0.3.0) from the PlatformIO registry even
# when an explicit entry of the same name is listed, so lib_deps got as far as
# "Installing M5GFX @ >=0.3.0" and stalled at 10% of it. Libraries found in
# lib/ satisfy those declarations first.
#
# The versions are PINNED to the ones EMBER's link map shows -- M5GFX 0.2.36 and
# M5Unified 0.2.29 -- and not to the unversioned directories, which hold 0.3.0.
#
# That is not fussiness. Building against 0.3.0 produced a firmware that ran but
# never brought the panel up: M5Cardputer 1.1.1's Keyboard.begin() reported
# "Unsupported board type: 136" instead of board_M5CardputerADV and the screen
# stayed black, while the same source on 0.2.36/0.2.29 boots straight to the
# title card. M5Cardputer 1.1.1 is written against M5Unified 0.2.x.
set -e
srcdir=${1:-$HOME/ember/.pio/libdeps/cardputer-adv}
cd "$(dirname "$0")/.."
mkdir -p lib

link() {  # link <name-in-lib> <directory-under-libdeps>
  if [ ! -d "$srcdir/$2" ]; then
    echo "missing $srcdir/$2" >&2
    exit 1
  fi
  ln -sfn "$srcdir/$2" "lib/$1"
  echo "lib/$1 -> $srcdir/$2"
}

link M5Cardputer M5Cardputer
link M5Unified  M5Unified@0.2.29
link M5GFX      M5GFX@0.2.36
link IRremote   IRremote
