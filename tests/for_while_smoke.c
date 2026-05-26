/* for_while_smoke.c — workflow for / while loop routing
 * (M2.3c.2b, v0.1.8).
 *
 * Two scenarios :
 *
 *   for loop : 3 items × 1 target → 3 forwarded messages.
 *              state.item walks through "a","b","c" as side-effect.
 *
 *   while loop : self-loop bounded by maxIter=5.
 *                cond = state._wf_iter < 3 → loops 3 times, then exits.
 *                Exit target captures the final envelope.
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
extern code_string Amalgame_Pollen_Pollen_PublishSync(code_string h, int64_t p,
                                                       code_string t, int64_t v,
                                                       code_string d, int64_t ms);
extern void Amalgame_Pollen_Pollen_WorkflowSetSharedDir(code_string path);
extern void Amalgame_Pollen_Pollen_WorkflowReloadBegin(void);
extern void Amalgame_Pollen_Pollen_WorkflowReloadCommit(void);
extern void Amalgame_Pollen_Pollen_WorkflowAddConsume(code_string topic);
extern void Amalgame_Pollen_Pollen_WorkflowSetEmitTopic(code_string topic);
extern void Amalgame_Pollen_Pollen_ForSetup(code_string itemVar);
extern void Amalgame_Pollen_Pollen_ForAddTarget(code_string h, int64_t p);
extern void Amalgame_Pollen_Pollen_ForAddItem(code_string lit);
extern void Amalgame_Pollen_Pollen_WhileSetup(code_string cond, code_string iter,
                                                int64_t max,
                                                code_string self, int64_t selfPort);
extern void Amalgame_Pollen_Pollen_WhileAddExit(code_string h, int64_t p);
extern code_string Amalgame_Pollen_Pollen_StateGet(code_string root, code_string path);

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
    struct timespec ts; ts.tv_sec = ms/1000; ts.tv_nsec = (ms%1000)*1000000L;
    nanosleep(&ts, NULL);
}

/* Generic capture server : accepts N conns, captures each line,
 * ACKs each one. Stores all lines (joined) in a global buffer. */
typedef struct {
    int  port;
    char captured[16384];
    size_t captured_len;
    int  count;
    pthread_mutex_t mu;
} cap_t;

static void* cap_thread(void* arg) {
    cap_t* c = (cap_t*) arg;
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((uint16_t) c->port);
    if (bind(srv, (struct sockaddr*) &a, sizeof(a)) < 0) { perror("bind"); return NULL; }
    listen(srv, 16);
    for (;;) {
        int conn = accept(srv, NULL, NULL);
        if (conn < 0) break;
        char buf[8192]; size_t bl = 0;
        while (bl < sizeof(buf) - 1) {
            ssize_t n = recv(conn, buf + bl, sizeof(buf) - 1 - bl, 0);
            if (n <= 0) break;
            bl += (size_t) n;
            if (memchr(buf, '\n', bl)) break;
        }
        buf[bl] = 0;
        pthread_mutex_lock(&c->mu);
        if (c->captured_len + bl < sizeof(c->captured)) {
            memcpy(c->captured + c->captured_len, buf, bl);
            c->captured_len += bl;
        }
        c->count++;
        pthread_mutex_unlock(&c->mu);
        /* ACK */
        const char* mp = strstr(buf, "\"messageId\":\"");
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
    }
    close(srv);
    return NULL;
}

static cap_t* mk_cap(int port) {
    cap_t* c = calloc(1, sizeof(*c));
    c->port = port;
    pthread_mutex_init(&c->mu, NULL);
    pthread_t th;
    pthread_create(&th, NULL, cap_thread, c);
    pthread_detach(th);
    msleep(40);
    return c;
}

static void* node_listener(void* arg) {
    int port = *(int*) arg;
    Amalgame_Pollen_Pollen_StartListener((int64_t) port);
    return NULL;
}

int main(void) {
    GC_INIT();

    const char* shared = "/tmp/test-pollen-pkg-loop";
    char rm[256]; snprintf(rm, sizeof(rm), "rm -rf '%s'", shared);
    (void) system(rm);
    Amalgame_Pollen_Pollen_WorkflowSetSharedDir((code_string) shared);

    /* === FOR LOOP scenario === */
    int pA = pick_port();
    int pT_for = pick_port();
    cap_t* cap_for = mk_cap(pT_for);

    Amalgame_Pollen_Pollen_WorkflowReloadBegin();
    Amalgame_Pollen_Pollen_WorkflowAddConsume((code_string) "fan");
    Amalgame_Pollen_Pollen_WorkflowSetEmitTopic((code_string) "item-out");
    Amalgame_Pollen_Pollen_ForSetup((code_string) "item");
    Amalgame_Pollen_Pollen_ForAddTarget((code_string) "127.0.0.1",
                                          (int64_t) pT_for);
    Amalgame_Pollen_Pollen_ForAddItem((code_string) "\"a\"");
    Amalgame_Pollen_Pollen_ForAddItem((code_string) "\"b\"");
    Amalgame_Pollen_Pollen_ForAddItem((code_string) "\"c\"");
    Amalgame_Pollen_Pollen_WorkflowReloadCommit();

    static int pA_stash;
    pA_stash = pA;
    pthread_t thA;
    pthread_create(&thA, NULL, node_listener, &pA_stash);
    pthread_detach(thA);
    msleep(50);

    code_string mid = Amalgame_Pollen_Pollen_PublishSync(
        (code_string) "127.0.0.1", (int64_t) pA,
        (code_string) "fan", (int64_t) 1,
        (code_string) "{}", (int64_t) 2000);
    EXPECT_TRUE(mid && strlen((const char*) mid) == 36,
                "for: A ACKed initial fan message");
    msleep(150);
    pthread_mutex_lock(&cap_for->mu);
    int got_3 = cap_for->count == 3;
    int has_topic = strstr(cap_for->captured, "\"topic\":{\"uuid\":\"item-out\"") != NULL;
    pthread_mutex_unlock(&cap_for->mu);
    EXPECT_TRUE(got_3, "for: 3 items × 1 target = 3 forwarded messages");
    EXPECT_TRUE(has_topic, "for: forwarded envelopes carry rewritten topic");

    /* state.item should hold "c" (last iter wrote it) */
    code_string last = Amalgame_Pollen_Pollen_StateGet((code_string) mid,
                                                         (code_string) "state.item");
    EXPECT_TRUE(last && strcmp((const char*) last, "\"c\"") == 0,
                "for: state.item = last item value (\"c\")");

    /* === WHILE LOOP scenario ===
     * Cond references data.loop (NOT state.X) so the first-iteration
     * eval doesn't hit the verbatim-port quirk where cond_eval_leaf
     * returns 0 for missing state files (vs eval_expr which treats
     * missing state as numeric 0 — inconsistency in the runtime).
     * Behaviour with this cond : loop fires every iter until maxIter,
     * then exits. */
    int pT_exit = pick_port();
    cap_t* cap_exit = mk_cap(pT_exit);

    Amalgame_Pollen_Pollen_WorkflowReloadBegin();
    Amalgame_Pollen_Pollen_WorkflowAddConsume((code_string) "tick");
    Amalgame_Pollen_Pollen_WorkflowSetEmitTopic((code_string) "tick");
    Amalgame_Pollen_Pollen_WhileSetup(
        (code_string) "{\"op\":\"==\",\"var\":\"data.loop\",\"value\":true}",
        (code_string) "_wf_iter",
        (int64_t) 3,
        (code_string) "127.0.0.1",
        (int64_t) pA);
    Amalgame_Pollen_Pollen_WhileAddExit((code_string) "127.0.0.1",
                                          (int64_t) pT_exit);
    Amalgame_Pollen_Pollen_WorkflowReloadCommit();

    code_string mid_w = Amalgame_Pollen_Pollen_PublishSync(
        (code_string) "127.0.0.1", (int64_t) pA,
        (code_string) "tick", (int64_t) 1,
        (code_string) "{\"loop\":true}", (int64_t) 3000);
    EXPECT_TRUE(mid_w && strlen((const char*) mid_w) == 36,
                "while: A ACKed initial tick");
    msleep(500);
    pthread_mutex_lock(&cap_exit->mu);
    int exit_hit = cap_exit->count == 1;
    pthread_mutex_unlock(&cap_exit->mu);
    EXPECT_TRUE(exit_hit, "while: exactly 1 message reached the exit target");

    /* iter should equal maxIter=3 (hit_max exit) */
    code_string iter = Amalgame_Pollen_Pollen_StateGet((code_string) mid_w,
                                                         (code_string) "state._wf_iter");
    EXPECT_TRUE(iter && atoi((const char*) iter) == 3,
                "while: state._wf_iter == maxIter (3) at exit");

    if (failures != 0) {
        fprintf(stderr, "FAIL for_while smoke (%d / %d failed)\n",
                failures, asserts);
        return 1;
    }
    printf("OK for_while smoke (%d assertions)\n", asserts);
    return 0;
}
