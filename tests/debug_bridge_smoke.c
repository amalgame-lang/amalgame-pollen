/* debug_bridge_smoke.c — M2.4 debug bridge phone-home + act
 * (v0.1.9).
 *
 * Topology :
 *   client ── MESSAGE (with debug field) ──> node A (consumes "in",
 *                                            self role "A", forwards
 *                                            to capture node B)
 *   node A, on a debug envelope where mode=breakpoint + role "A" ∈
 *   breakpoints, phones home to a MOCK MANAGER on :mgrPort, sends
 *   DEBUG_PAUSE, and acts on the reply.
 *
 * We stand up a mock manager that records the DEBUG_PAUSE it
 * receives and replies with a scripted command. Three scenarios :
 *   1. CONTINUE → A forwards (mode rewritten to "breakpoint")
 *   2. CANCEL   → A drops (B receives nothing)
 *   3. MUTATE   → A rewrites data + forwards
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
extern void        Amalgame_Pollen_Pollen_WorkflowSetSelf(code_string role, code_string host, int64_t port);
extern void        Amalgame_Pollen_Pollen_WorkflowReloadBegin(void);
extern void        Amalgame_Pollen_Pollen_WorkflowReloadCommit(void);
extern void        Amalgame_Pollen_Pollen_WorkflowAddConsume(code_string topic);
extern void        Amalgame_Pollen_Pollen_WorkflowAddNext(code_string host, int64_t port);

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

/* ── Mock manager : accept one DEBUG_PAUSE, record it, reply with a
 * scripted command line. ── */
typedef struct {
    int  port;
    char reply[512];      /* scripted command line incl. trailing \n */
    char received[8192];  /* the DEBUG_PAUSE we got */
    size_t received_len;
    int  pause_count;
    pthread_mutex_t mu;
} mock_mgr_t;

static void* mock_mgr_thread(void* arg) {
    mock_mgr_t* m = (mock_mgr_t*) arg;
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((uint16_t) m->port);
    if (bind(srv, (struct sockaddr*) &a, sizeof(a)) < 0) { perror("bind mgr"); return NULL; }
    listen(srv, 4);
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
        pthread_mutex_lock(&m->mu);
        memcpy(m->received, buf, bl < sizeof(m->received) ? bl : sizeof(m->received)-1);
        m->received_len = bl;
        m->pause_count++;
        pthread_mutex_unlock(&m->mu);
        send(conn, m->reply, strlen(m->reply), 0);
        close(conn);
    }
    close(srv);
    return NULL;
}

static mock_mgr_t* mk_mgr(int port, const char* reply) {
    mock_mgr_t* m = calloc(1, sizeof(*m));
    m->port = port;
    snprintf(m->reply, sizeof(m->reply), "%s", reply);
    pthread_mutex_init(&m->mu, NULL);
    pthread_t th;
    pthread_create(&th, NULL, mock_mgr_thread, m);
    pthread_detach(th);
    msleep(40);
    return m;
}

/* ── Capture node B : record one forwarded envelope + ACK it. ── */
typedef struct {
    int port;
    char captured[8192];
    size_t captured_len;
    int count;
    pthread_mutex_t mu;
} cap_t;

static void* cap_thread(void* arg) {
    cap_t* c = (cap_t*) arg;
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((uint16_t) c->port);
    if (bind(srv, (struct sockaddr*) &a, sizeof(a)) < 0) { perror("bind cap"); return NULL; }
    listen(srv, 4);
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
        memcpy(c->captured, buf, bl < sizeof(c->captured) ? bl : sizeof(c->captured)-1);
        c->captured_len = bl;
        c->count++;
        pthread_mutex_unlock(&c->mu);
        const char* mp = strstr(buf, "\"messageId\":\"");
        if (mp) { mp += 13; const char* mq = strchr(mp, '"');
            if (mq) { char ack[256]; int al = snprintf(ack, sizeof(ack),
                "{\"type\":\"ACK\",\"messageId\":\"%.*s\",\"timestamp\":0}\n",
                (int)(mq-mp), mp); send(conn, ack, (size_t) al, 0); } }
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

static void* node_a_listener(void* arg) {
    Amalgame_Pollen_Pollen_StartListener((int64_t) *(int*) arg);
    return NULL;
}

/* Build + send a debug envelope to node A via a raw socket.
 * Returns 1 if A ACKed (we got a line back). */
static int send_debug_msg(int aPort, int mgrPort, const char* data) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((uint16_t) aPort);
    if (connect(fd, (struct sockaddr*) &a, sizeof(a)) < 0) { close(fd); return 0; }
    char env[2048];
    int n = snprintf(env, sizeof(env),
        "{\"messageId\":\"11111111-1111-4111-8111-111111111111\","
         "\"rootMessageId\":\"11111111-1111-4111-8111-111111111111\","
         "\"type\":\"MESSAGE\","
         "\"topic\":{\"uuid\":\"in\",\"version\":1},"
         "\"data\":%s,\"timestamp\":0,"
         "\"debug\":{\"session\":\"sess-1\",\"manager\":\"127.0.0.1:%d\","
                    "\"mode\":\"breakpoint\",\"hit_bp\":false,"
                    "\"breakpoints\":[{\"role\":\"A\"}]}}\n",
        data, mgrPort);
    send(fd, env, (size_t) n, 0);
    char rbuf[512];
    struct timeval tv = { 3, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ssize_t r = recv(fd, rbuf, sizeof(rbuf)-1, 0);
    close(fd);
    return r > 0;
}

static void reset_cap(cap_t* c) {
    pthread_mutex_lock(&c->mu);
    c->captured_len = 0; c->count = 0; c->captured[0] = 0;
    pthread_mutex_unlock(&c->mu);
}

int main(void) {
    GC_INIT();

    int aPort   = pick_port();
    int bPort   = pick_port();
    cap_t* capB = mk_cap(bPort);

    /* Configure node A : role "A", consume "in", forward to B. */
    Amalgame_Pollen_Pollen_WorkflowSetSelf((code_string) "A",
                                             (code_string) "127.0.0.1",
                                             (int64_t) aPort);
    Amalgame_Pollen_Pollen_WorkflowReloadBegin();
    Amalgame_Pollen_Pollen_WorkflowAddConsume((code_string) "in");
    Amalgame_Pollen_Pollen_WorkflowAddNext((code_string) "127.0.0.1", (int64_t) bPort);
    Amalgame_Pollen_Pollen_WorkflowReloadCommit();

    static int aStash; aStash = aPort;
    pthread_t thA;
    pthread_create(&thA, NULL, node_a_listener, &aStash);
    pthread_detach(thA);
    msleep(60);

    /* === Scenario 1 : CONTINUE → A forwards, mode rewritten === */
    {
        int mgrPort = pick_port();
        mock_mgr_t* mgr = mk_mgr(mgrPort, "{\"type\":\"DEBUG_CONTINUE\"}\n");
        reset_cap(capB);
        int acked = send_debug_msg(aPort, mgrPort, "{\"v\":1}");
        EXPECT_TRUE(acked, "continue: A ACKed the debug message");
        msleep(200);
        pthread_mutex_lock(&mgr->mu);
        int got_pause = mgr->pause_count == 1
            && strstr(mgr->received, "\"type\":\"DEBUG_PAUSE\"") != NULL
            && strstr(mgr->received, "\"role\":\"A\"") != NULL
            && strstr(mgr->received, "\"session\":\"sess-1\"") != NULL;
        pthread_mutex_unlock(&mgr->mu);
        EXPECT_TRUE(got_pause, "continue: manager received well-formed DEBUG_PAUSE");
        pthread_mutex_lock(&capB->mu);
        int fwd = capB->count == 1;
        int mode_rw = strstr(capB->captured, "\"mode\":\"breakpoint\"") != NULL;
        pthread_mutex_unlock(&capB->mu);
        EXPECT_TRUE(fwd, "continue: B received the forwarded envelope");
        EXPECT_TRUE(mode_rw, "continue: forwarded mode rewritten to breakpoint");
    }

    /* === Scenario 2 : CANCEL → A drops, B gets nothing === */
    {
        int mgrPort = pick_port();
        mock_mgr_t* mgr = mk_mgr(mgrPort, "{\"type\":\"DEBUG_CANCEL\"}\n");
        reset_cap(capB);
        int acked = send_debug_msg(aPort, mgrPort, "{\"v\":2}");
        EXPECT_TRUE(acked, "cancel: A still ACKed (cancel only drops forward)");
        msleep(200);
        pthread_mutex_lock(&mgr->mu);
        int got_pause = mgr->pause_count == 1;
        pthread_mutex_unlock(&mgr->mu);
        EXPECT_TRUE(got_pause, "cancel: manager received the pause");
        pthread_mutex_lock(&capB->mu);
        int dropped = capB->count == 0;
        pthread_mutex_unlock(&capB->mu);
        EXPECT_TRUE(dropped, "cancel: B received NOTHING (message dropped)");
    }

    /* === Scenario 3 : MUTATE → A rewrites data + forwards === */
    {
        int mgrPort = pick_port();
        mock_mgr_t* mgr = mk_mgr(mgrPort,
            "{\"type\":\"DEBUG_MUTATE\",\"data\":{\"v\":999},\"then\":\"continue\"}\n");
        reset_cap(capB);
        int acked = send_debug_msg(aPort, mgrPort, "{\"v\":3}");
        EXPECT_TRUE(acked, "mutate: A ACKed");
        msleep(200);
        pthread_mutex_lock(&capB->mu);
        int fwd = capB->count == 1;
        int mutated = strstr(capB->captured, "\"data\":{\"v\":999}") != NULL;
        int orig_gone = strstr(capB->captured, "\"v\":3") == NULL;
        pthread_mutex_unlock(&capB->mu);
        EXPECT_TRUE(fwd, "mutate: B received the forwarded envelope");
        EXPECT_TRUE(mutated, "mutate: data rewritten to {\"v\":999}");
        EXPECT_TRUE(orig_gone, "mutate: original data {\"v\":3} replaced");
    }

    /* === Scenario 4 : no debug field → forwards normally (no
     *     manager contact needed) === */
    {
        reset_cap(capB);
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in a; memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons((uint16_t) aPort);
        connect(fd, (struct sockaddr*) &a, sizeof(a));
        const char* env =
            "{\"messageId\":\"22222222-2222-4222-8222-222222222222\","
             "\"rootMessageId\":\"22222222-2222-4222-8222-222222222222\","
             "\"type\":\"MESSAGE\",\"topic\":{\"uuid\":\"in\",\"version\":1},"
             "\"data\":{\"plain\":true},\"timestamp\":0}\n";
        send(fd, env, strlen(env), 0);
        char rbuf[256];
        struct timeval tv = { 2, 0 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        recv(fd, rbuf, sizeof(rbuf)-1, 0);
        close(fd);
        msleep(150);
        pthread_mutex_lock(&capB->mu);
        int fwd = capB->count == 1 && strstr(capB->captured, "\"plain\":true") != NULL;
        pthread_mutex_unlock(&capB->mu);
        EXPECT_TRUE(fwd, "no-debug: plain message forwarded without manager contact");
    }

    if (failures != 0) {
        fprintf(stderr, "FAIL debug_bridge smoke (%d / %d failed)\n", failures, asserts);
        return 1;
    }
    printf("OK debug_bridge smoke (%d assertions)\n", asserts);
    return 0;
}
