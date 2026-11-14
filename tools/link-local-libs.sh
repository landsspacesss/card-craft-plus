#!/bin/sh
# Points lib/ at the M5Stack library trees that already build for this board.
#
# These four are not in lib_deps: Library Dependency Finder resolves a DECLARED
# dependency (M5Unified wants M5GFX >= 0.3.0) from the PlatformIO registry even
# when an explicit entry of the same name is listed, so lib_deps got as far as
# "Installing M5GFX @ >=0.3.0" and stalled at 10% of it. Libraries found in
# lib/ satisfy those declarations first.
#
# The source tree is the one the EMBER build resolved for the same hardware:
# M5Cardputer 1.1.1 (which is where the ADV's TCA8418 keyboard lives),
# M5Unified 0.2.29, M5GFX 0.3.0 and IRremote.
set -e
srcdir=${1:-$HOME/ember/.pio/libdeps/cardputer-adv}
cd "$(dirname "$0")/.."
mkdir -p lib
for d in M5Cardputer M5Unified M5GFX IRremote; do
  if [ ! -d "$srcdir/$d" ]; then
    echo "missing $srcdir/$d" >&2
    exit 1
  fi
  ln -sfn "$srcdir/$d" "lib/$d"
  echo "lib/$d -> $srcdir/$d"
done
