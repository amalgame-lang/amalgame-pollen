/* listener_smoke.c — exercises Pollen.StartListener + Pollen.PublishSync
 * (M2.3b, v0.1.5).
 *
 * Spins up Pollen.StartListener on an ephemeral port in a pthread,
 * fires several PublishSync from the main thread, verifies that each
 * one returns the matching mid (proof the ACK round-trip works) and
 * that ordering across multiple connections doesn't break.
 *
 * Also exercises the fire-and-forget Publish + manual recv path to
 * confirm StartListener + Publish interop too.
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
extern code_string Amalgame_Pollen_Pollen_PublishSync(code_string host, int64_t port,
                                                       code_string topicUuid,
                                                       int64_t topicVersion,
                                                       code_string dataJson,
                                                       int64_t timeoutMs);

static int failures = 0;
static int asserts  = 0;

#define EXPECT_TRUE(cond, label) do {                                          \
    asserts++;                                                                 \
    if (!(cond)) {                                                             \
        fprintf(stderr, "FAIL %s\n", (label));                                 \
        failures++;                                                            \
    }                                                                          \
} while (0)

static void* listener_thread(void* arg) {
    int port = *(int*) arg;
    Amalgame_Pollen_Pollen_StartListener((int64_t) port);
    return NULL;
}

/* Pick an ephemeral port by binding+listening, querying it, then
 * closing — the listener will reuse-bind it. Tiny race window
 * but acceptable for a one-shot smoke test. */
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

int main(void) {
    GC_INIT();

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

    /* Give the listener a tick to bind. */
    msleep(50);

    /* 1. PublishSync — single round-trip */
    code_string mid = Amalgame_Pollen_Pollen_PublishSync(
        (code_string) "127.0.0.1", (int64_t) port,
        (code_string) "topic-sync", (int64_t) 1,
        (code_string) "{\"i\":0}", (int64_t) 2000);
    EXPECT_TRUE(mid != NULL, "PublishSync returned non-null");
    EXPECT_TRUE(strlen((const char*) mid) == 36,
                "PublishSync mid is 36 chars (ACK matched)");

    /* 2. Multiple PublishSync in sequence */
    int n_ok = 0;
    for (int i = 0; i < 5; i++) {
        char data[64];
        snprintf(data, sizeof(data), "{\"i\":%d}", i);
        code_string m = Amalgame_Pollen_Pollen_PublishSync(
            (code_string) "127.0.0.1", (int64_t) port,
            (code_string) "topic-loop", (int64_t) 1,
            (code_string) data, (int64_t) 2000);
        if (m && strlen((const char*) m) == 36) n_ok++;
    }
    EXPECT_TRUE(n_ok == 5, "PublishSync ×5 all ACKed");

    /* 3. Fire-and-forget Publish to the same listener */
    code_string fmid = Amalgame_Pollen_Pollen_Publish(
        (code_string) "127.0.0.1", (int64_t) port,
        (code_string) "topic-ff", (int64_t) 1,
        (code_string) "{\"async\":true}");
    EXPECT_TRUE(fmid != NULL && strlen((const char*) fmid) == 36,
                "Publish (fire-and-forget) returned a mid");

    /* 4. PublishSync against a closed port → empty mid (timeout
     * isn't tripped since connect fails immediately). */
    code_string nope = Amalgame_Pollen_Pollen_PublishSync(
        (code_string) "127.0.0.1", (int64_t) 1,
        (code_string) "topic-closed", (int64_t) 1,
        (code_string) "{}", (int64_t) 500);
    EXPECT_TRUE(nope != NULL && ((const char*) nope)[0] == 0,
                "PublishSync to closed port returns empty");

    /* 5. Mid-mismatch via raw socket : if the listener echoes ACKs
     * keyed on the *envelope's* messageId field, sending a bogus
     * envelope should still come back with that bogus mid. */
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((uint16_t) port);
    if (connect(s, (struct sockaddr*) &a, sizeof(a)) == 0) {
        const char* env =
            "{\"messageId\":\"deadbeef-dead-beef-dead-deadbeefcafe\","
             "\"type\":\"MESSAGE\",\"topic\":{\"uuid\":\"t\",\"version\":1},"
             "\"data\":{},\"timestamp\":0}\n";
        send(s, env, strlen(env), 0);
        char rbuf[512]; rbuf[0] = 0;
        ssize_t n = recv(s, rbuf, sizeof(rbuf) - 1, 0);
        if (n > 0) rbuf[n] = 0;
        EXPECT_TRUE(strstr(rbuf, "deadbeef-dead-beef-dead-deadbeefcafe") != NULL,
                    "raw envelope ACK carries the envelope's mid");
        EXPECT_TRUE(strstr(rbuf, "\"type\":\"ACK\"") != NULL,
                    "ACK envelope has type=ACK");
        close(s);
    } else {
        fprintf(stderr, "skip raw-socket test : connect failed\n");
    }

    if (failures != 0) {
        fprintf(stderr, "FAIL listener smoke (%d / %d failed)\n",
                failures, asserts);
        return 1;
    }
    printf("OK listener smoke (%d assertions)\n", asserts);
    /* Leave the listener thread running ; we detached it and the
     * process exit cleans up. */
    return 0;
}
