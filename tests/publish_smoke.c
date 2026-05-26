/* publish_smoke.c — exercises Pollen.Publish() (M2.3a, v0.1.4).
 *
 * Stands up a tiny ephemeral TCP listener in a thread, calls
 * Amalgame_Pollen_Pollen_Publish, verifies the listener received
 * a well-formed envelope containing the returned messageId.
 *
 * No dependency on amc here — Publish hits a regular sockaddr_in
 * server, so a pthread-only mock peer is enough to validate the
 * publisher hot path end-to-end (envelope shape, UUID minting,
 * connect/send/close lifecycle).
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

extern code_string Amalgame_Pollen_Pollen_Publish(code_string host, int64_t port,
                                                    code_string topicUuid,
                                                    int64_t topicVersion,
                                                    code_string dataJson);

static int      failures = 0;
static int      asserts  = 0;
static char     captured[8192];
static size_t   captured_len = 0;
static int      listen_port = 0;

#define EXPECT_TRUE(cond, label) do {                                          \
    asserts++;                                                                 \
    if (!(cond)) {                                                             \
        fprintf(stderr, "FAIL %s\n", (label));                                 \
        failures++;                                                            \
    }                                                                          \
} while (0)

static void* server_thread(void* arg) {
    int srv = *(int*) arg;
    int conn = accept(srv, NULL, NULL);
    if (conn < 0) return NULL;
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

static int start_listener(pthread_t* th) {
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) return -1;
    int one = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr; memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;  /* ephemeral */
    if (bind(srv, (struct sockaddr*) &addr, sizeof(addr)) < 0) { close(srv); return -1; }
    if (listen(srv, 1) < 0) { close(srv); return -1; }
    socklen_t alen = sizeof(addr);
    getsockname(srv, (struct sockaddr*) &addr, &alen);
    listen_port = ntohs(addr.sin_port);

    static int srv_fd_stash;
    srv_fd_stash = srv;
    if (pthread_create(th, NULL, server_thread, &srv_fd_stash) != 0) {
        close(srv);
        return -1;
    }
    return 0;
}

static int is_uuid_v4(const char* s) {
    if (strlen(s) != 36) return 0;
    for (int i = 0; i < 36; i++) {
        char c = s[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return 0;
        } else {
            int hex = (c >= '0' && c <= '9') ||
                      (c >= 'a' && c <= 'f');
            if (!hex) return 0;
        }
    }
    /* v4 marker : 15th char (after 3 dashes at idx 8,13,18) is at i=14 */
    if (s[14] != '4') return 0;
    /* variant : 20th char in [8,9,a,b] */
    char v = s[19];
    if (v != '8' && v != '9' && v != 'a' && v != 'b') return 0;
    return 1;
}

int main(void) {
    GC_INIT();

    pthread_t th;
    if (start_listener(&th) != 0) {
        fprintf(stderr, "FAIL : couldn't start listener\n");
        return 1;
    }

    code_string mid = Amalgame_Pollen_Pollen_Publish(
        (code_string) "127.0.0.1", (int64_t) listen_port,
        (code_string) "topic-test", (int64_t) 1,
        (code_string) "{\"hello\":\"world\"}");

    /* Wait for the server thread to finish receiving + close. */
    pthread_join(th, NULL);

    EXPECT_TRUE(mid != NULL, "Publish returned non-null mid");
    EXPECT_TRUE(strlen((const char*) mid) == 36, "mid is 36 chars");
    EXPECT_TRUE(is_uuid_v4((const char*) mid), "mid is valid UUIDv4");

    /* Listener received the envelope */
    EXPECT_TRUE(captured_len > 0, "listener received bytes");
    EXPECT_TRUE(strstr(captured, "\"type\":\"MESSAGE\"") != NULL,
                "envelope has type MESSAGE");
    EXPECT_TRUE(strstr(captured, "\"topic\":{\"uuid\":\"topic-test\",\"version\":1}") != NULL,
                "envelope topic + version");
    EXPECT_TRUE(strstr(captured, "\"data\":{\"hello\":\"world\"}") != NULL,
                "envelope data verbatim");

    /* The returned mid appears in the envelope as both messageId
     * AND rootMessageId (originator pattern). */
    char needle[128];
    snprintf(needle, sizeof(needle), "\"messageId\":\"%s\"", (const char*) mid);
    EXPECT_TRUE(strstr(captured, needle) != NULL,
                "envelope messageId matches returned mid");
    snprintf(needle, sizeof(needle), "\"rootMessageId\":\"%s\"", (const char*) mid);
    EXPECT_TRUE(strstr(captured, needle) != NULL,
                "envelope rootMessageId == mid");

    EXPECT_TRUE(captured[captured_len - 1] == '\n', "envelope is newline-terminated");

    /* Connection refused → empty mid, no crash. */
    code_string mid2 = Amalgame_Pollen_Pollen_Publish(
        (code_string) "127.0.0.1", (int64_t) 1,  /* port 1 = closed */
        (code_string) "topic-refused", (int64_t) 1,
        (code_string) "{}");
    EXPECT_TRUE(mid2 != NULL && ((const char*) mid2)[0] == 0,
                "connect-refused returns empty string");

    if (failures != 0) {
        fprintf(stderr, "FAIL publish smoke (%d / %d failed)\n", failures, asserts);
        return 1;
    }
    printf("OK publish smoke (%d assertions)\n", asserts);
    return 0;
}
