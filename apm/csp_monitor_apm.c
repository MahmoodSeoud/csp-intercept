/*
 * csp_monitor_apm.c - CSH APM entry for csp-intercept's promiscuous link monitor.
 *
 * WIRING SKELETON (Increment 2): apm_init + one registered slash command -- enough
 * to prove the APM builds, loads (`apm load -p . -s csp-intercept`), and registers
 * its command (`apm info`). The background drainer, the frozen-schema CSV writer,
 * the per-flow seq/RTT measurement (lib/ci_meas), and the MEASUREMENT_SUSPECT
 * window flag land next -- see the approved plan, Increment 2 / apm.
 */
#include <stdio.h>

#include <apm/apm.h>
#include <slash/slash.h>
#include <csp/csp.h>

int apm_init(void) {
    return 0;
}

static int csp_monitor_cmd(struct slash *slash) {
    (void)slash;
    printf("csp-intercept: monitor APM loaded (drainer not yet implemented)\n");
    return SLASH_SUCCESS;
}
slash_command(csp_monitor, csp_monitor_cmd, "", "csp-intercept link monitor (stub)");
