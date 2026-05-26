/* workflow_dispatch_smoke.c — 2-node workflow dispatch (M2.3c.1, v0.1.6).
 *
 * Topology :
 *
 *   client -- MESSAGE topic="in" --> node A (port pA)
 *                                    A consumes "in", emits "out",
 *                                    forwards to node B (port pB).
 *                                    A ACKs client.
 *   node A -- MESSAGE topic="out" --> node B (port pB)
 *                                     B is free-form (no workflow),
 *                                     ACKs A's forwarded envelope.
 *
 * We can't easily verify B received A's forward through the
 * package's own listener (it ACKs but doesn't expose received
 * messages). So node B is replaced by a hand-rolled mock TCP
 * server in this test that captures whatever A forwards to it,
 * then asserts on the envelope shape : new mid (different from
 * client's mid), parentMessageId == client's mid, topic="out".
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
extern code_string Amalgame_Pollen_Pollen_PublishSync(code_string host, int64_t port,
                                                       code_string topicUuid,
                                                       int64_t topicVersion,
                                                       code_string dataJson,
                                                       int64_t timeoutMs);
extern void Amalgame_Pollen_Pollen_WorkflowReloadBegin(void);
extern void Amalgame_Pollen_Pollen_WorkflowReloadCommit(void);
extern void Amalgame_Pollen_Pollen_WorkflowAddConsume(code_string topic);
extern void Amalgame_Pollen_Pollen_WorkflowAddNext(code_string host, int64_t port);
extern void Amalgame_Pollen_Pollen_WorkflowSetEmitTopic(code_string topic);
extern int64_t Amalgame_Pollen_Pollen_WorkflowVersion(void);

static int failures = 0;
static int asserts  = 0;

#define EXPECT_TRUE(cond, label) do {                                          \
    asserts++;                                                                 \
    if (!(cond)) { fprintf(stderr, "FAIL %s\n", (label)); failures++; }         \
} while (0)

static int pick_port(void) {
    int s = socket(AF_INET, SOCK_STREAM, 0); if (s < 0) return 0;
    struct sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(s, (struct sockaddr*) &a, sizeof(a));
    socklen_t alen = sizeof(a);
    getsockname(s, (struct sockaddr*) &a, &alen);
    int p = ntohs(a.sin_port);
    close(s);
    return p;
}

static void msleep(int ms) {
    struct timespec ts; ts.tv_sec = ms / 1000; ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static void* node_a_listener(void* arg) {
    int port = *(int*) arg;
    Amalgame_Pollen_Pollen_StartListener((int64_t) port);
    return NULL;
}

/* Hand-rolled mock node B : accept ONE connection, recv one
 * newline-terminated envelope into the global captured_b buffer,
 * send back an ACK, close. Single-shot is enough for the test. */
static char     captured_b[8192];
static size_t   captured_b_len = 0;
static int      bind_b_port = 0;

static void* node_b_mock(void* arg) {
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((uint16_t) bind_b_port);
    if (bind(srv, (struct sockaddr*) &a, sizeof(a)) < 0) {
        perror("bind B"); return NULL;
    }
    listen(srv, 4);
    int conn = accept(srv, NULL, NULL);
    if (conn < 0) { close(srv); return NULL; }
    while (captured_b_len < sizeof(captured_b) - 1) {
        ssize_t n = recv(conn, captured_b + captured_b_len,
                          sizeof(captured_b) - 1 - captured_b_len, 0);
        if (n <= 0) break;
        captured_b_len += (size_t) n;
        if (memchr(captured_b, '\n', captured_b_len)) break;
    }
    captured_b[captured_b_len] = 0;
    /* ACK back so node A's forward connect completes cleanly. */
    const char* mp = strstr(captured_b, "\"messageId\":\"");
    if (mp) {
        mp += 13;
        const char* mq = strchr(mp, '"');
        if (mq) {
            char ack[256];
            int alen = snprintf(ack, sizeof(ack),
                "{\"type\":\"ACK\",\"messageId\":\"%.*s\",\"timestamp\":0}\n",
                (int)(mq - mp), mp);
            send(conn, ack, (size_t) alen, 0);
        }
    }
    close(conn);
    close(srv);
    (void) arg;
    return NULL;
}

int main(void) {
    GC_INIT();

    int pA = pick_port();
    int pB = pick_port();
    if (pA <= 0 || pB <= 0) { fprintf(stderr, "FAIL : pick ports\n"); return 1; }
    bind_b_port = pB;

    /* Spin up B (the mock capture endpoint) FIRST so A's forward
     * connect doesn't race. */
    pthread_t thB;
    pthread_create(&thB, NULL, node_b_mock, NULL);
    msleep(50);

    /* Configure A's workflow : consume "in", emit "out", forward to
     * 127.0.0.1:pB. */
    Amalgame_Pollen_Pollen_WorkflowReloadBegin();
    Amalgame_Pollen_Pollen_WorkflowAddConsume((code_string) "in");
    Amalgame_Pollen_Pollen_WorkflowAddNext((code_string) "127.0.0.1", (int64_t) pB);
    Amalgame_Pollen_Pollen_WorkflowSetEmitTopic((code_string) "out");
    Amalgame_Pollen_Pollen_WorkflowReloadCommit();

    EXPECT_TRUE(Amalgame_Pollen_Pollen_WorkflowVersion() >= 1,
                "WorkflowVersion bumped after reload commit");

    /* Start A's listener. */
    static int pA_stash;
    pA_stash = pA;
    pthread_t thA;
    pthread_create(&thA, NULL, node_a_listener, &pA_stash);
    pthread_detach(thA);
    msleep(50);

    /* Client → A : MESSAGE topic="in" */
    code_string mid_client = Amalgame_Pollen_Pollen_PublishSync(
        (code_string) "127.0.0.1", (int64_t) pA,
        (code_string) "in", (int64_t) 1,
        (code_string) "{\"v\":7}", (int64_t) 2000);
    EXPECT_TRUE(mid_client != NULL && strlen((const char*) mid_client) == 36,
                "A ACKed client (topic in consumes)");

    /* Wait for B's capture thread to receive the forward. */
    pthread_join(thB, NULL);

    /* Verify B got a rewritten envelope */
    EXPECT_TRUE(captured_b_len > 0, "B received forwarded envelope");
    EXPECT_TRUE(strstr(captured_b, "\"topic\":{\"uuid\":\"out\"") != NULL,
                "forwarded topic rewritten to 'out'");
    EXPECT_TRUE(strstr(captured_b, "\"parentMessageId\":\"") != NULL,
                "forwarded envelope has parentMessageId");

    /* parent mid in B's envelope == client mid */
    char needle[80];
    snprintf(needle, sizeof(needle), "\"parentMessageId\":\"%s\"",
             (const char*) mid_client);
    EXPECT_TRUE(strstr(captured_b, needle) != NULL,
                "parentMessageId chains to client's mid");

    /* New mid in B differs from client's mid */
    const char* fmp = strstr(captured_b, "\"messageId\":\"");
    if (fmp) {
        fmp += 13;
        const char* fmq = strchr(fmp, '"');
        if (fmq && (fmq - fmp) == 36) {
            EXPECT_TRUE(memcmp(fmp, (const char*) mid_client, 36) != 0,
                        "forwarded mid is fresh (≠ client mid)");
        }
    }

    /* Data passed through verbatim */
    EXPECT_TRUE(strstr(captured_b, "\"data\":{\"v\":7}") != NULL,
                "data field preserved across forward");

    /* === Negative test : publish a non-consumed topic to A.
     *     Expectation : A drops it silently (no ACK), so
     *     PublishSync times out with empty mid. */
    code_string drop = Amalgame_Pollen_Pollen_PublishSync(
        (code_string) "127.0.0.1", (int64_t) pA,
        (code_string) "unknown-topic", (int64_t) 1,
        (code_string) "{}", (int64_t) 300);
    EXPECT_TRUE(drop != NULL && ((const char*) drop)[0] == 0,
                "A drops non-consumed topic (no ACK, timeout)");

    if (failures != 0) {
        fprintf(stderr, "FAIL workflow_dispatch smoke (%d / %d failed)\n",
                failures, asserts);
        return 1;
    }
    printf("OK workflow_dispatch smoke (%d assertions)\n", asserts);
    return 0;
}
