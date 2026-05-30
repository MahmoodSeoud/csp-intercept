# oracle_join.awk - assert two-oracle agreement for a DTP loss run.
#
# Partitions the `total` sent fragments into the proxy drop-log's dropped set and the
# APM CSV's observed set, and verifies they cover every fragment exactly once. A real
# link-loss measurement is only trustworthy if the two independent oracles agree: a
# dropped fragment must NOT have reached the monitor, and an observed one must NOT be
# in the drop-log. Pass `-v total=<N>` (sent fragment count).
#
# Usage:
#   awk -F, '$8==1{print $6}' drop.csv          > dropped_idx   # drop-log frag idx
#   grep -av '^#' apm.csv | awk -F, '{print $11}'> observed_idx  # APM observed frag idx
#   awk -v total=<N> -f scripts/oracle_join.awk dropped_idx observed_idx
#
# Exit status 0 = perfect agreement, 1 = mismatch.
NR == FNR { dropped[$1] = 1; nd++; next }
{ observed[$1] = 1; no++ }
END {
    if (total == 0) { print "ERROR: pass -v total=<sent fragment count>"; exit 2 }
    for (i in dropped) if (i in observed) overlap++
    for (i = 0; i < total; i++) {
        if ((i in dropped) && (i in observed)) both++
        else if ((i in dropped) || (i in observed)) covered++
        else missing++
    }
    printf "dropped=%d observed=%d (sent=%d)\n", nd, no, total
    printf "dropped frags that also reached APM [must be 0]: %d\n", overlap + 0
    printf "frags 0..%d: covered_exactly_once=%d in_both=%d in_neither=%d\n",
           total - 1, covered + 0, both + 0, missing + 0
    ok = (overlap == 0 && covered == total && both == 0 && missing == 0)
    printf "VERDICT: %s\n", ok ? "PERFECT TWO-ORACLE AGREEMENT" : "MISMATCH"
    exit ok ? 0 : 1
}
