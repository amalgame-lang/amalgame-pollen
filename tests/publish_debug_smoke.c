/* publish_debug_smoke.c — Pollen.PublishDebug (M4, v0.1.12).
 *
 * Verifies the debug envelope shape : a capture server receives a
 * MESSAGE with a `debug` field carrying session / mode / manager /
 * breakpoints, plus messageId == rootMessageId (originator).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "_runtime.h"

extern code_string Amalgame_Pollen_Pollen_PublishDebug(code_string host, int64_t port,
                                                        code_string topicUuid, int64_t topicVersion,
                                                        code_string dataJson,
                                                        code_string session, code_string mode,
                                                        code_string breakpointsJson,
                                                        code_string managerAddr);

static int failures = 0, asserts = 0;
#define EXPECT_TRUE(c, l) do { asserts++; if (!(c)) { fprintf(stderr, "FAIL %s\n", (l)); failures++; } } while (0)

static char captured[8192];
static size_t captured_len = 0;
static int cap_port = 0;

static void* cap_thread(void* arg) {
    (void) arg;
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1; setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((uint16_t) cap_port);
    if (bind(srv, (struct sockaddr*) &a, sizeof(a)) < 0) return NULL;
    listen(srv, 4);
    int c = accept(srv, NULL, NULL);
    if (c >= 0) {
        while (captured_len < sizeof(captured) - 1) {
            ssize_t n = recv(c, captured + captured_len, sizeof(captured) - 1 - captured_len, 0);
            if (n <= 0) break;
            captured_len += n;
            if (memchr(captured, '\n', captured_len)) break;
        }
        captured[captured_len] = 0;
        char* mp = strstr(captured, "\"messageId\":\"");
        if (mp) { mp += 13; char* mq = strchr(mp, '"');
            if (mq) { char ack[128]; int al = snprintf(ack, sizeof(ack),
                "{\"type\":\"ACK\",\"messageId\":\"%.*s\"}\n", (int)(mq-mp), mp);
                send(c, ack, al, 0); } }
        close(c);
    }
    close(srv);
    return NULL;
}

static int pick_port(void) {
    int s = socket(AF_INET, SOCK_STREAM, 0); if (s < 0) return 0;
    struct sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(s, (struct sockaddr*) &a, sizeof(a));
    socklen_t al = sizeof(a); getsockname(s, (struct sockaddr*) &a, &al);
    int p = ntohs(a.sin_port); close(s); return p;
}

int main(void) {
    GC_INIT();
    cap_port = pick_port();
    pthread_t th; pthread_create(&th, NULL, cap_thread, NULL);
    struct timespec ts = {0, 50000000L}; nanosleep(&ts, NULL);

    code_string mid = Amalgame_Pollen_Pollen_PublishDebug(
        (code_string) "127.0.0.1", (int64_t) cap_port,
        (code_string) "order.in", (int64_t) 1,
        (code_string) "{\"amount\":1500}",
        (code_string) "sess-xyz", (code_string) "breakpoint",
        (code_string) "[{\"role\":\"vip\",\"when\":\"data.amount>1000\"}]",
        (code_string) "127.0.0.1:3001");

    pthread_join(th, NULL);

    EXPECT_TRUE(mid && strlen((const char*) mid) == 36, "PublishDebug returned mid");
    EXPECT_TRUE(strstr(captured, "\"type\":\"MESSAGE\"") != NULL, "envelope type MESSAGE");
    EXPECT_TRUE(strstr(captured, "\"debug\":{") != NULL, "envelope has debug field");
    EXPECT_TRUE(strstr(captured, "\"session\":\"sess-xyz\"") != NULL, "debug session present");
    EXPECT_TRUE(strstr(captured, "\"mode\":\"breakpoint\"") != NULL, "debug mode present");
    EXPECT_TRUE(strstr(captured, "\"manager\":\"127.0.0.1:3001\"") != NULL, "debug manager present");
    EXPECT_TRUE(strstr(captured, "\"when\":\"data.amount>1000\"") != NULL, "breakpoint cond spliced");
    EXPECT_TRUE(strstr(captured, "\"data\":{\"amount\":1500}") != NULL, "data verbatim");
    /* messageId == rootMessageId */
    char needle[128];
    snprintf(needle, sizeof(needle), "\"rootMessageId\":\"%s\"", (const char*) mid);
    EXPECT_TRUE(strstr(captured, needle) != NULL, "rootMessageId == mid (originator)");

    if (failures) { fprintf(stderr, "FAIL publish_debug (%d/%d)\n", failures, asserts); return 1; }
    printf("OK publish_debug smoke (%d assertions)\n", asserts);
    return 0;
}
