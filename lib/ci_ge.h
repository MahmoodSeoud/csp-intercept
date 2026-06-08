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

/*
 * PER-ATTEMPT Gilbert-Elliott state machine (T7) -- the recovery-measurement sibling
 * of ci_ge_fill above.
 *
 * ci_ge_fill advances the chain along the packet INDEX axis: vec[i] is keyed to the
 * fragment's identity, so the SAME fragment is dropped on EVERY retransmit. That is
 * exactly right for validating the monitor against a one-shot synthetic stream (each
 * index emitted once, byte-identical two-oracle), but it makes recovery IMPOSSIBLE:
 * pointed at a real transfer, a re-requested fragment is re-dropped forever, so a
 * loss-recovering uploader (satDeploy) would look no better than a naive one -- a
 * silent false-null, the worst outcome for a measurement instrument. See the pivot in
 * mseo-master-plan-measurement-20260607.md (CORRECTION 2026-06-08, point 3).
 *
 * This state machine advances the SAME 2-state chain along the TRANSMISSION axis: one
 * draw per call to ci_ge_state_step(), independent of which fragment is being sent. A
 * re-sent fragment therefore gets a FRESH draw, so a fragment dropped on one attempt
 * can succeed on the next -- recovery is measurable. The loss becomes a property of the
 * CHANNEL (its good/bad run over time) rather than of the fragment identity, which is
 * the physically correct model for an intermittent link.
 *
 * Reproducibility is preserved the same way ci_ge_fill achieves it: the chain is a
 * pure function of (seed, step count). ci_ge_state_init mixes the seed identically to
 * ci_ge_fill, so for a given seed the step-axis trajectory is BYTE-IDENTICAL to the
 * index-axis vector ci_ge_fill produces -- same seed => same channel replayed across
 * the upload run and the satDeploy run (a fair, replayable both-arms loss source). The
 * ONLY difference between the two is the axis the chain is advanced along.
 */
typedef struct {
    uint64_t seed;   /* splitmix-mixed seed (mirrors ci_ge_fill's one-shot mix)     */
    uint64_t step;   /* transmission counter = the per-call draw index              */
    uint64_t t_g2b;  /* P(GOOD->BAD) as a splitmix threshold (draw < t fires)       */
    uint64_t t_b2g;  /* P(BAD->GOOD) as a splitmix threshold                        */
    int      bad;    /* current chain state: 0 = GOOD (keep), 1 = BAD (drop)        */
} ci_ge_state_t;

/*
 * Initialise a per-attempt chain in GOOD under `seed` and transitions (p_g2b, p_b2g),
 * derived from a target loss + mean burst via ci_ge_params (same as ci_ge_fill). The
 * seed is mixed exactly as ci_ge_fill mixes it, so the step trajectory equals
 * ci_ge_fill(seed, ...) read along index. No-op if st is NULL.
 */
void ci_ge_state_init(ci_ge_state_t *st, uint64_t seed, double p_g2b, double p_b2g);

/*
 * Advance the chain by one transmission and return its drop decision (1 = drop this
 * transmission, 0 = keep). Emits the CURRENT state, then transitions with one draw --
 * so the first call always keeps (the chain starts in GOOD), matching ci_ge_fill's
 * vec[0]. Returns 0 if st is NULL.
 */
int ci_ge_state_step(ci_ge_state_t *st);

#endif /* CI_GE_H */
