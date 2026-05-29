/*
 * ci_prng.h - deterministic, portable PRNG for reproducible drop decisions.
 *
 * Why not rand(): libc rand() differs across platforms/libc, so "same seed ->
 * same drops" fails off the author's machine. splitmix64 is a fixed algorithm:
 * identical output on every OS. (Eng review decision 5 / TR-5.)
 *
 * Key idea: draw exactly ONE u64 per packet INDEX, keyed by (seed, index).
 * Because the draw is keyed by index (not consumed sequentially), a corruption
 * or delay branch can never desync the loss stream -- the drop-decision vector
 * is a pure function of (seed, index). That vector is the reproducibility gate.
 */
#ifndef CI_PRNG_H
#define CI_PRNG_H

#include <stdint.h>

/* Canonical splitmix64 (Vigna). State-in, mixed-value-out. */
static inline uint64_t ci_splitmix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

/* One independent draw for packet `index` under `seed`. */
static inline uint64_t ci_draw(uint64_t seed, uint64_t index) {
    return ci_splitmix64(seed ^ index);
}

#endif /* CI_PRNG_H */
