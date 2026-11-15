// Census of the world representation: how many columns are a simple solid
// prefix, and how many carry gaps (overhangs, caves, tunnels). See SLIM.md.
// Build: g++ -std=gnu++17 -I src tools/world-census.cpp src/world.cpp -o /tmp/census
#include <cstdio>
#include <initializer_list>
#include "world.h"
int main(){
  for (uint32_t seed : {4242u, 7u, 99u}) {
    world::generate(seed);
    world::rebuildLight();
    int cols=0, holes=0, prefixish=0;
    for (int y=0;y<world::H;++y) for (int x=0;x<world::W;++x) {
      ++cols;
      // walk z from 0..31 and detect "gap then solid again"
      bool seenAir=false, gap=false;
      for (int z=0; z<32; ++z) {
        const bool s = world::solidAt(x,y,z);
        if (!s) seenAir=true; else if (seenAir) { gap=true; break; }
      }
      if (gap) ++holes; else ++prefixish;
    }
    printf("seed %u: columns=%d  prefix-solid=%d  with-gaps=%d\n", seed, cols, prefixish, holes);
  }
  return 0;
}
