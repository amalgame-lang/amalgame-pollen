/* lb_smoke.c — capability registry + power-of-two load balancer
 * (Phase 6.2 / 6.3, v0.1.18).
 *
 * Phase 1 : write fake capability files (2 fresh + 1 stale + 1 for a
 *           different topic) into a temp sharedDir, start the reader,
 *           assert RegistrySize counts only the fresh ones.
 * Phase 2 : ResolveProvider picks the lower-inFlight provider of a
 *           topic (deterministic with 2 candidates) ; unknown topic
 *           resolves to "".
 * Phase 3 : end-to-end — a node emitting "lb.topic" with LB enabled
 *           forwards to the lower-inFlight provider (a mock capture
 *           endpoint), not the static next.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "_runtime.h"

extern void        Amalgame_Pollen_Pollen_WorkflowSetSharedDir(code_string path);
extern void        Amalgame_Pollen_Pollen_StartCapabilityReader(void);
extern void        Amalgame_Pollen_Pollen_SetLoadBalance(int on);
extern int64_t     Amalgame_Pollen_Pollen_RegistrySize(void);
extern code_string Amalgame_Pollen_Pollen_ResolveProvider(code_string topic);
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
extern void Amalgame_Pollen_Pollen_CondBranchOpen(code_string condJson);
extern void Amalgame_Pollen_Pollen_CondBranchSetTopic(code_string topic);

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

static int64_t now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void write_cap(const char* capdir, const char* id, const char* host,
                       int port, const char* action, long inflight,
                       int64_t heartbeat) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.json", capdir, id);
    char buf[1024];
    int n = snprintf(buf, sizeof(buf),
        "{\"instanceId\":\"%s\",\"host\":\"%s\",\"port\":%d,"
         "\"label\":\"%s\",\"actions\":[\"%s\"],"
         "\"load\":{\"inFlight\":%ld,\"msgsHandled\":0},"
         "\"heartbeat\":%lld,\"version\":\"amalgame-pollen\"}\n",
        id, host, port, id, action, inflight, (long long) heartbeat);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) { ssize_t w = write(fd, buf, (size_t) n); (void) w; close(fd); }
}

static char     captured[8192];
static size_t   captured_len = 0;
static int      mock_port = 0;
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
    close(conn);
    close(srv);
    return NULL;
}

static void* node_listener(void* arg) {
    int port = *(int*) arg;
    Amalgame_Pollen_Pollen_StartListener((int64_t) port);
    return NULL;
}

int main(void) {
    GC_INIT();

    char shared[] = "/tmp/pollen-lb-XXXXXX";
    if (!mkdtemp(shared)) { fprintf(stderr, "FAIL mkdtemp\n"); return 1; }
    char capdir[1100];
    snprintf(capdir, sizeof(capdir), "%s/capabilities", shared);
    mkdir(capdir, 0755);
    Amalgame_Pollen_Pollen_WorkflowSetSharedDir((code_string) shared);

    int pA = pick_port();   /* provider A : lb.topic, inFlight 5 */
    int pB = pick_port();   /* provider B : lb.topic, inFlight 1 (winner) */
    int pD = pick_port();   /* provider D : other.topic */

    int64_t now = now_ms();
    write_cap(capdir, "provA", "127.0.0.1", pA, "lb.topic", 5, now);
    write_cap(capdir, "provB", "127.0.0.1", pB, "lb.topic", 1, now);
    write_cap(capdir, "provD", "127.0.0.1", pD, "other.topic", 0, now);
    /* stale : heartbeat 30s old → excluded */
    write_cap(capdir, "provStale", "127.0.0.1", 9999, "lb.topic", 0, now - 30000);

    /* ---- Phase 1 : registry build + staleness ---- */
    Amalgame_Pollen_Pollen_StartCapabilityReader();
    msleep(500);  /* first scan runs immediately on thread start */
    EXPECT_TRUE(Amalgame_Pollen_Pollen_RegistrySize() == 3,
                "registry has 3 fresh providers (stale excluded)");

    /* ---- Phase 2 : resolve (power-of-two, deterministic) ---- */
    char expectB[64];
    snprintf(expectB, sizeof(expectB), "127.0.0.1:%d", pB);
    code_string r1 = Amalgame_Pollen_Pollen_ResolveProvider((code_string) "lb.topic");
    EXPECT_TRUE(r1 != NULL && strcmp((const char*) r1, expectB) == 0,
                "ResolveProvider picks the lower-inFlight provider (B)");

    char expectD[64];
    snprintf(expectD, sizeof(expectD), "127.0.0.1:%d", pD);
    code_string r2 = Amalgame_Pollen_Pollen_ResolveProvider((code_string) "other.topic");
    EXPECT_TRUE(r2 != NULL && strcmp((const char*) r2, expectD) == 0,
                "ResolveProvider returns the sole provider of a topic");

    code_string r3 = Amalgame_Pollen_Pollen_ResolveProvider((code_string) "no.such.topic");
    EXPECT_TRUE(r3 != NULL && ((const char*) r3)[0] == 0,
                "ResolveProvider returns \"\" for an unknown topic");

    /* ---- Phase 3 : LB forward goes to the winner, not static next ---- */
    /* B is the lower-inFlight provider of lb.topic ; stand up its mock
     * capture endpoint and a throwaway static next that must NOT be hit. */
    mock_port = pB; captured_len = 0; captured[0] = 0;
    pthread_t thB; pthread_create(&thB, NULL, mock_capture, NULL);
    msleep(50);

    int pNode = pick_port();
    int pStaticNext = pick_port();  /* would be used if LB were off */
    Amalgame_Pollen_Pollen_SetLoadBalance(1);
    Amalgame_Pollen_Pollen_WorkflowReloadBegin();
    Amalgame_Pollen_Pollen_WorkflowAddConsume((code_string) "in");
    Amalgame_Pollen_Pollen_WorkflowAddNext((code_string) "127.0.0.1", (int64_t) pStaticNext);
    Amalgame_Pollen_Pollen_WorkflowSetEmitTopic((code_string) "lb.topic");
    Amalgame_Pollen_Pollen_WorkflowReloadCommit();

    static int pNode_stash; pNode_stash = pNode;
    pthread_t thNode; pthread_create(&thNode, NULL, node_listener, &pNode_stash);
    pthread_detach(thNode);
    msleep(50);

    Amalgame_Pollen_Pollen_PublishSync(
        (code_string) "127.0.0.1", (int64_t) pNode,
        (code_string) "in", (int64_t) 1,
        (code_string) "{\"v\":1}", (int64_t) 2000);

    pthread_join(thB, NULL);
    EXPECT_TRUE(captured_len > 0,
                "LB forwarded to the resolved provider (B), not the static next");
    EXPECT_TRUE(strstr(captured, "\"topic\":{\"uuid\":\"lb.topic\"") != NULL,
                "LB-forwarded envelope carries the emit topic");

    /* ---- Phase 5 : v2 cond branch routes by topic (registry) ---- */
    /* A cond branch with a topic set (CondBranchSetTopic) must rebuild
     * with that topic and forward to a registry-resolved provider,
     * not a static target. Reuse provider B (lb.topic) as the branch
     * target ; an always-true branch (empty cond) routes there. */
    mock_port = pB; captured_len = 0; captured[0] = 0;
    pthread_t thB2; pthread_create(&thB2, NULL, mock_capture, NULL);
    msleep(50);

    int pCond = pick_port();
    Amalgame_Pollen_Pollen_SetLoadBalance(1);
    Amalgame_Pollen_Pollen_WorkflowReloadBegin();
    Amalgame_Pollen_Pollen_WorkflowAddConsume((code_string) "cin");
    Amalgame_Pollen_Pollen_CondBranchOpen((code_string) "");   /* always-true / else */
    Amalgame_Pollen_Pollen_CondBranchSetTopic((code_string) "lb.topic");
    Amalgame_Pollen_Pollen_WorkflowReloadCommit();

    static int pCond_stash; pCond_stash = pCond;
    pthread_t thCond; pthread_create(&thCond, NULL, node_listener, &pCond_stash);
    pthread_detach(thCond);
    msleep(50);

    Amalgame_Pollen_Pollen_PublishSync(
        (code_string) "127.0.0.1", (int64_t) pCond,
        (code_string) "cin", (int64_t) 1,
        (code_string) "{\"v\":2}", (int64_t) 2000);
    pthread_join(thB2, NULL);
    EXPECT_TRUE(captured_len > 0,
                "cond branch with a topic routed to a registry-resolved provider");
    EXPECT_TRUE(strstr(captured, "\"topic\":{\"uuid\":\"lb.topic\"") != NULL,
                "cond branch rebuilt the envelope with the branch topic");

    if (failures != 0) {
        fprintf(stderr, "FAIL lb smoke (%d / %d failed)\n", failures, asserts);
        return 1;
    }
    printf("OK lb smoke (%d assertions)\n", asserts);
    return 0;
}
