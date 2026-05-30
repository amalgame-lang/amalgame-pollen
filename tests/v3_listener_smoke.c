/* v3_listener_smoke.c — Pollen v3 Phase 3d integration smoke.
 *
 * Spins up Pollen.StartListener on an ephemeral port in a pthread,
 * loads the v3 dispatch fixture, publishes an envelope at a topic
 * a v3 entry consumes ("tick.hourly"), and asserts the listener
 * routed the message to the v3 dispatcher exactly once.
 *
 * Then publishes another envelope on an unrelated topic and checks
 * the dispatch count does NOT advance — proves the lookup is
 * discriminating, not "anything goes".
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "_runtime.h"

extern void        Amalgame_Pollen_Pollen_StartListener(int64_t port);
extern code_string Amalgame_Pollen_Pollen_Publish(code_string host, int64_t port,
                                                    code_string topicUuid,
                                                    int64_t topicVersion,
                                                    code_string dataJson);
extern code_bool   Amalgame_Pollen_Pollen_WorkflowLoadV3(code_string path);
extern code_bool   Amalgame_Pollen_Pollen_WorkflowV3IsActive(void);
extern int64_t     Amalgame_Pollen_Pollen_WorkflowV3DispatchCount(void);
extern void        Amalgame_Pollen_Pollen_WorkflowV3ResetDispatchCount(void);

static int failures = 0;
static int asserts  = 0;

#define EXPECT_TRUE(cond, label) do {                                          \
    asserts++;                                                                 \
    if (!(cond)) {                                                             \
        fprintf(stderr, "FAIL : %s\n", label);                                 \
        failures++;                                                            \
    }                                                                          \
} while (0)

#define EXPECT_EQ_I(got, want, label) do {                                     \
    asserts++;                                                                 \
    long long g = (long long)(got), w = (long long)(want);                     \
    if (g != w) {                                                              \
        fprintf(stderr, "FAIL : %s (got %lld, want %lld)\n", label, g, w);     \
        failures++;                                                            \
    }                                                                          \
} while (0)

static void* listener_thread(void* arg) {
    int port = *(int*) arg;
    Amalgame_Pollen_Pollen_StartListener((int64_t) port);
    return NULL;
}

/* Pick an ephemeral port — borrowed from listener_smoke.c. */
static int pick_ephemeral_port(void) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return 0;
    struct sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    bind(s, (struct sockaddr*) &a, sizeof(a));
    socklen_t alen = sizeof(a);
    getsockname(s, (struct sockaddr*) &a, &alen);
    int port = ntohs(a.sin_port);
    close(s);
    return port;
}

static void msleep(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

/* Poll the dispatch counter up to total_ms, return what we observed. */
static int64_t wait_for_count(int64_t want, int total_ms) {
    int elapsed = 0;
    while (elapsed < total_ms) {
        int64_t n = Amalgame_Pollen_Pollen_WorkflowV3DispatchCount();
        if (n >= want) return n;
        msleep(20);
        elapsed += 20;
    }
    return Amalgame_Pollen_Pollen_WorkflowV3DispatchCount();
}

int main(void) {
    GC_INIT();

    /* Load the v3 fixture (has a `by-topic` entry consuming
     * "tick.hourly"). */
    code_bool ok = Amalgame_Pollen_Pollen_WorkflowLoadV3(
        (code_string) "examples/workflow-v3-dispatch-fixture.json");
    EXPECT_TRUE(ok, "WorkflowLoadV3 OK");
    EXPECT_TRUE(Amalgame_Pollen_Pollen_WorkflowV3IsActive(),
                 "WorkflowV3IsActive");

    Amalgame_Pollen_Pollen_WorkflowV3ResetDispatchCount();
    EXPECT_EQ_I(Amalgame_Pollen_Pollen_WorkflowV3DispatchCount(), 0,
                 "dispatch count initially 0");

    int port = pick_ephemeral_port();
    if (port <= 0) { fprintf(stderr, "FAIL : pick port\n"); return 1; }

    static int port_stash;
    port_stash = port;
    pthread_t th;
    if (pthread_create(&th, NULL, listener_thread, &port_stash) != 0) {
        fprintf(stderr, "FAIL : start listener thread\n");
        return 1;
    }
    pthread_detach(th);
    /* Let the listener bind + accept-loop spin up. */
    msleep(150);

    /* Publish to a v3-consumed topic. The Publish helper waits for
     * the connect + sends synchronously, but the listener-side
     * dispatch is async — we poll. */
    code_string mid = Amalgame_Pollen_Pollen_Publish(
        (code_string) "127.0.0.1", (int64_t) port,
        (code_string) "tick.hourly", 1,
        (code_string) "{\"n\":21,\"user\":{\"id\":42}}");
    EXPECT_TRUE(mid && strlen((const char*) mid) == 36,
                 "publish returned a 36-char mid");

    int64_t n1 = wait_for_count(1, 1500);
    EXPECT_EQ_I(n1, 1, "dispatch count after v3 publish");

    /* Publish to an unrelated topic — no v3 entry consumes it. */
    code_string mid2 = Amalgame_Pollen_Pollen_Publish(
        (code_string) "127.0.0.1", (int64_t) port,
        (code_string) "no.such.topic", 1,
        (code_string) "{}");
    EXPECT_TRUE(mid2 && strlen((const char*) mid2) == 36,
                 "unrelated publish also returned a mid");
    msleep(250);
    EXPECT_EQ_I(Amalgame_Pollen_Pollen_WorkflowV3DispatchCount(), 1,
                 "dispatch count unchanged by unrelated topic");

    if (failures == 0) {
        printf("OK v3 listener smoke (%d assertions)\n", asserts);
        return 0;
    }
    fprintf(stderr, "FAIL : %d of %d assertions\n", failures, asserts);
    return 1;
}
