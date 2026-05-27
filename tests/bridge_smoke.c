/* bridge_smoke.c — Mosaic bridge hooks (v0.1.17).
 *
 * Exercises the three new runtime entry points :
 *
 *   OnMessage(handler)  — transform a consumed message's data before
 *                         forward. Phase 1 asserts the downstream
 *                         peer receives the TRANSFORMED data (+ the
 *                         usual mid/parent/topic rewrite). Phase 2
 *                         asserts a handler returning "" DROPS the
 *                         message (no forward).
 *   OnComplete(handler) — fires when a consumed message terminates
 *                         at a leaf node (no nexts). Phase 3 asserts
 *                         the handler ran and got the final envelope.
 *   Forward(env, data)  — out-of-band re-emit from a non-worker
 *                         thread. Phase 4 asserts the peer receives a
 *                         fresh-mid, parent-chained, data-swapped
 *                         envelope on the configured emit topic.
 *
 * Closures are hand-built the way amc lowers them : an
 * AmalgameClosure { fn, env } whose fn matches AmalgameClosure1Fn
 * (void* fn(void* env, void* arg)). The arg is the envelope JSON
 * (code_string) ; OnMessage's return is the new data JSON.
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
extern void Amalgame_Pollen_Pollen_OnMessage(AmalgameClosure* handler);
extern void Amalgame_Pollen_Pollen_OnComplete(AmalgameClosure* handler);
extern code_string Amalgame_Pollen_Pollen_Forward(code_string envelopeJson,
                                                   code_string newDataJson);

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

static void* node_listener(void* arg) {
    int port = *(int*) arg;
    Amalgame_Pollen_Pollen_StartListener((int64_t) port);
    return NULL;
}

/* === Hand-built closures ================================== */

/* OnMessage transform : ignore input, return a fixed new data. */
static void* xform_replace(void* env, void* arg) {
    (void) env; (void) arg;
    char* nd = (char*) GC_MALLOC_ATOMIC(32);
    strcpy(nd, "{\"transformed\":true}");
    return (void*) nd;
}

/* OnMessage transform : return "" → the worker drops the message. */
static void* xform_drop(void* env, void* arg) {
    (void) env; (void) arg;
    char* nd = (char*) GC_MALLOC_ATOMIC(1);
    nd[0] = 0;
    return (void*) nd;
}

/* OnComplete handler : capture the final envelope + set a flag. */
static volatile int  completed = 0;
static char          completed_env[8192];
static void* on_complete_capture(void* env, void* arg) {
    (void) env;
    const char* e = (const char*) arg;
    if (e) { strncpy(completed_env, e, sizeof(completed_env) - 1); }
    completed = 1;
    return NULL;
}

/* === Single-shot mock capture endpoint ==================== */
static char     captured[8192];
static size_t   captured_len = 0;
static int      mock_port = 0;

static void reset_capture(void) { captured_len = 0; captured[0] = 0; }

static void* mock_capture(void* arg) {
    (void) arg;
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((uint16_t) mock_port);
    if (bind(srv, (struct sockaddr*) &a, sizeof(a)) < 0) { perror("bind mock"); return NULL; }
    listen(srv, 4);
    int conn = accept(srv, NULL, NULL);
    if (conn < 0) { close(srv); return NULL; }
    while (captured_len < sizeof(captured) - 1) {
        ssize_t n = recv(conn, captured + captured_len,
                          sizeof(captured) - 1 - captured_len, 0);
        if (n <= 0) break;
        captured_len += (size_t) n;
        if (memchr(captured, '\n', captured_len)) break;
    }
    captured[captured_len] = 0;
    const char* mp = strstr(captured, "\"messageId\":\"");
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
    return NULL;
}

int main(void) {
    GC_INIT();

    AmalgameClosure* h_replace  = AmalgameClosure_new((void*) xform_replace, NULL);
    AmalgameClosure* h_drop     = AmalgameClosure_new((void*) xform_drop, NULL);
    AmalgameClosure* h_complete = AmalgameClosure_new((void*) on_complete_capture, NULL);

    /* ---- Phase 1 : OnMessage transform then forward ---- */
    int pNode = pick_port();
    int pPeer = pick_port();
    if (pNode <= 0 || pPeer <= 0) { fprintf(stderr, "FAIL pick ports\n"); return 1; }

    mock_port = pPeer; reset_capture();
    pthread_t thPeer; pthread_create(&thPeer, NULL, mock_capture, NULL);
    msleep(50);

    Amalgame_Pollen_Pollen_OnMessage(h_replace);
    Amalgame_Pollen_Pollen_WorkflowReloadBegin();
    Amalgame_Pollen_Pollen_WorkflowAddConsume((code_string) "in");
    Amalgame_Pollen_Pollen_WorkflowAddNext((code_string) "127.0.0.1", (int64_t) pPeer);
    Amalgame_Pollen_Pollen_WorkflowSetEmitTopic((code_string) "out");
    Amalgame_Pollen_Pollen_WorkflowReloadCommit();

    static int pNode_stash; pNode_stash = pNode;
    pthread_t thNode; pthread_create(&thNode, NULL, node_listener, &pNode_stash);
    pthread_detach(thNode);
    msleep(50);

    code_string mid1 = Amalgame_Pollen_Pollen_PublishSync(
        (code_string) "127.0.0.1", (int64_t) pNode,
        (code_string) "in", (int64_t) 1,
        (code_string) "{\"v\":7}", (int64_t) 2000);
    EXPECT_TRUE(mid1 != NULL && strlen((const char*) mid1) == 36,
                "node ACKed publish (topic consumed)");

    pthread_join(thPeer, NULL);
    EXPECT_TRUE(captured_len > 0, "peer received forwarded envelope");
    EXPECT_TRUE(strstr(captured, "\"data\":{\"transformed\":true}") != NULL,
                "OnMessage transformed the data before forward");
    EXPECT_TRUE(strstr(captured, "\"v\":7") == NULL,
                "original data replaced (not just appended)");
    EXPECT_TRUE(strstr(captured, "\"topic\":{\"uuid\":\"out\"") != NULL,
                "topic still rewritten to emit topic");
    EXPECT_TRUE(strstr(captured, "\"parentMessageId\":\"") != NULL,
                "forwarded envelope chains parentMessageId");

    /* ---- Phase 2 : OnMessage returns "" → drop (no forward) ---- */
    int pNode2 = pick_port();
    int pPeer2 = pick_port();
    mock_port = pPeer2; reset_capture();
    pthread_t thPeer2; pthread_create(&thPeer2, NULL, mock_capture, NULL);
    msleep(50);

    Amalgame_Pollen_Pollen_OnMessage(h_drop);
    Amalgame_Pollen_Pollen_WorkflowReloadBegin();
    Amalgame_Pollen_Pollen_WorkflowAddConsume((code_string) "in2");
    Amalgame_Pollen_Pollen_WorkflowAddNext((code_string) "127.0.0.1", (int64_t) pPeer2);
    Amalgame_Pollen_Pollen_WorkflowSetEmitTopic((code_string) "out2");
    Amalgame_Pollen_Pollen_WorkflowReloadCommit();

    static int pNode2_stash; pNode2_stash = pNode2;
    pthread_t thNode2; pthread_create(&thNode2, NULL, node_listener, &pNode2_stash);
    pthread_detach(thNode2);
    msleep(50);

    /* node still ACKs (consume matched) even though it drops forward. */
    Amalgame_Pollen_Pollen_PublishSync(
        (code_string) "127.0.0.1", (int64_t) pNode2,
        (code_string) "in2", (int64_t) 1,
        (code_string) "{\"x\":1}", (int64_t) 1000);
    msleep(200);  /* give any (erroneous) forward time to arrive */
    EXPECT_TRUE(captured_len == 0,
                "OnMessage returning \"\" drops the message (no forward)");
    /* tidy : the mock is still blocked in accept() — close via a
     * throwaway connect so its thread exits. */
    {
        int c = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in a; memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons((uint16_t) pPeer2);
        if (connect(c, (struct sockaddr*) &a, sizeof(a)) == 0) {
            send(c, "{}\n", 3, 0);
        }
        close(c);
    }
    pthread_join(thPeer2, NULL);

    /* ---- Phase 3 : OnComplete fires at a leaf node ---- */
    Amalgame_Pollen_Pollen_OnMessage(NULL);       /* clear transform */
    Amalgame_Pollen_Pollen_OnComplete(h_complete);
    int pLeaf = pick_port();

    Amalgame_Pollen_Pollen_WorkflowReloadBegin();
    Amalgame_Pollen_Pollen_WorkflowAddConsume((code_string) "done");
    /* no AddNext → this role is a leaf */
    Amalgame_Pollen_Pollen_WorkflowReloadCommit();

    static int pLeaf_stash; pLeaf_stash = pLeaf;
    pthread_t thLeaf; pthread_create(&thLeaf, NULL, node_listener, &pLeaf_stash);
    pthread_detach(thLeaf);
    msleep(50);

    completed = 0; completed_env[0] = 0;
    code_string midC = Amalgame_Pollen_Pollen_PublishSync(
        (code_string) "127.0.0.1", (int64_t) pLeaf,
        (code_string) "done", (int64_t) 1,
        (code_string) "{\"final\":42}", (int64_t) 2000);
    EXPECT_TRUE(midC != NULL && strlen((const char*) midC) == 36,
                "leaf node ACKed publish");
    msleep(150);
    EXPECT_TRUE(completed == 1, "OnComplete fired at the leaf node");
    EXPECT_TRUE(strstr(completed_env, "\"rootMessageId\":\"") != NULL,
                "OnComplete envelope carries rootMessageId");
    EXPECT_TRUE(strstr(completed_env, "\"final\":42") != NULL,
                "OnComplete envelope carries the final data");

    /* ---- Phase 4 : Forward re-emits out-of-band ---- */
    Amalgame_Pollen_Pollen_OnComplete(NULL);
    int pFwd = pick_port();
    mock_port = pFwd; reset_capture();
    pthread_t thFwd; pthread_create(&thFwd, NULL, mock_capture, NULL);
    msleep(50);

    Amalgame_Pollen_Pollen_WorkflowReloadBegin();
    Amalgame_Pollen_Pollen_WorkflowAddConsume((code_string) "any");
    Amalgame_Pollen_Pollen_WorkflowAddNext((code_string) "127.0.0.1", (int64_t) pFwd);
    Amalgame_Pollen_Pollen_WorkflowSetEmitTopic((code_string) "fwdout");
    Amalgame_Pollen_Pollen_WorkflowReloadCommit();

    const char* in_env =
        "{\"messageId\":\"11111111-1111-4111-8111-111111111111\","
         "\"rootMessageId\":\"11111111-1111-4111-8111-111111111111\","
         "\"type\":\"MESSAGE\","
         "\"topic\":{\"uuid\":\"any\",\"version\":1},"
         "\"data\":{\"old\":1},\"timestamp\":0}";
    code_string fmid = Amalgame_Pollen_Pollen_Forward(
        (code_string) in_env, (code_string) "{\"f\":9}");
    EXPECT_TRUE(fmid != NULL && strlen((const char*) fmid) == 36,
                "Forward returns a fresh mid");
    pthread_join(thFwd, NULL);
    EXPECT_TRUE(captured_len > 0, "Forward delivered to the peer");
    EXPECT_TRUE(strstr(captured, "\"data\":{\"f\":9}") != NULL,
                "Forward swapped the data");
    EXPECT_TRUE(strstr(captured, "\"topic\":{\"uuid\":\"fwdout\"") != NULL,
                "Forward rewrote the topic to the emit topic");
    EXPECT_TRUE(strstr(captured,
                "\"parentMessageId\":\"11111111-1111-4111-8111-111111111111\"") != NULL,
                "Forward chains parentMessageId to the incoming mid");

    if (failures != 0) {
        fprintf(stderr, "FAIL bridge smoke (%d / %d failed)\n", failures, asserts);
        return 1;
    }
    printf("OK bridge smoke (%d assertions)\n", asserts);
    return 0;
}
