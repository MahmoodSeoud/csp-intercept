/*
 * ci_ge.h - Gilbert-Elliott burst-loss generator for the deterministic injector.
 *
 * The injector's reproducibility invariant (ci_prng.h / ci_rule.h): a packet's drop
 * decision is a PURE FUNCTION of (seed, index) - arrival order, retransmits, and
 * delays must never desync the loss stream. A stateful Markov chain advanced
 * per-arrival would break that.
 *
 * So Gilbert-Elliott is a *generator*, not a new hot path: ci_ge_fill() precomputes a
 * deterministic drop vector by running the 2-state chain along the INDEX axis (one
 * splitmix64 draw per index for the transition). The caller hands that vector to
 * ci_drop_rule_t.replay_vector, and the existing, already-tested replay path in
 * ci_rule_decide() applies it - byte-identical across runs/OSes, order-independent by
 * construction. No change to the reproducibility-critical decision path.
 *
 *   GOOD --p_g2b--> BAD        GOOD is lossless, BAD always drops (classic Gilbert-
 *   GOOD <--p_b2g-- BAD        Elliott burst loss).
 *
 * Steady-state marginal loss = p_g2b / (p_g2b + p_b2g); mean burst length = 1/p_b2g
 * (geometric). ci_ge_params() derives (p_g2b, p_b2g) from a target marginal loss and a
 * target mean burst length - the knob the sweep actually wants ("X% loss in bursts of
 * ~B packets"). See mseo-master-plan-measurement-20260607.md (the pivot).
 */
#ifndef CI_GE_H
#define CI_GE_H

#include <stddef.h>
#include <stdint.h>

/*
 * Derive transition probabilities from a target marginal `loss` in [0,1] and a target
 * `mean_burst` length (>= 1, in packets). Writes p_g2b = P(GOOD->BAD) and
 * p_b2g = P(BAD->GOOD). Inputs are clamped: loss<=0 -> p_g2b=0 (never enters BAD);
 * loss>=1 -> p_g2b=1, p_b2g=0 (always BAD); mean_burst<1 -> treated as 1.
 */
void ci_ge_params(double loss, double mean_burst, double *p_g2b, double *p_b2g);

/*
 * Fill vec[0..n) with a deterministic Gilbert-Elliott drop vector (1=drop, 0=keep)
 * under `seed` and transitions (p_g2b, p_b2g). The chain starts in GOOD. vec[i] is a
 * pure function of (seed, p_g2b, p_b2g, i): identical output on every run and OS.
 * No-op if vec is NULL or n is 0.
 */
void ci_ge_fill(uint64_t seed, double p_g2b, double p_b2g, uint8_t *vec, size_t n);

#endif /* CI_GE_H */
