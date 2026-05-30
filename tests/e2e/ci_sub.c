/*
 * ci_sub - a discard subscriber for the proxy E2E.
 *
 * Connects a ZMQ_SUB to the proxy backend (XPUB) and subscribes to everything.
 * Its subscription propagates back through the proxy's items[1] (XPUB->XSUB) path
 * to the publisher -- which is exactly what makes published frames flow. So this
 * also keeps the XSUB/XPUB subscription-forwarding path exercised: if items[1] were
 * broken, no frames would ever reach the proxy and the drop-log would be empty.
 *
 * Prints the count of frames received to stdout on exit (the forwarding-regression
 * test asserts this is non-zero). Runs until SIGTERM/SIGINT.
 * Usage: ci_sub <connect-addr>  (default tcp://localhost:7000)
 */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <zmq.h>

static volatile sig_atomic_t run = 1;
static void on_sig(int s) { (void)s; run = 0; }

int main(int argc, char ** argv) {
    const char * addr = (argc > 1) ? argv[1] : "tcp://localhost:7000";
    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);

    void * ctx = zmq_ctx_new();
    void * sub = zmq_socket(ctx, ZMQ_SUB);
    if (zmq_connect(sub, addr) != 0) {
        return 1;
    }
    zmq_setsockopt(sub, ZMQ_SUBSCRIBE, "", 0);

    long received = 0;
    zmq_msg_t msg;
    while (run) {
        zmq_msg_init(&msg);
        int rc = zmq_msg_recv(&msg, sub, ZMQ_DONTWAIT);
        zmq_msg_close(&msg);
        if (rc < 0) {
            usleep(2000);   /* nothing pending; back off and re-check the signal */
        } else {
            received++;
        }
    }
    zmq_close(sub);
    zmq_ctx_destroy(ctx);
    printf("%ld\n", received);
    return 0;
}
