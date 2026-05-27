/* recorder_smoke.c — M2.4b executions/ step recorder (v0.1.11).
 *
 * A package node configured with a workflow + shared dir should
 * write one <sharedDir>/executions/<mid>-<role>.json record per
 * forwarded hop. Verifies the file appears with the expected
 * fields (role, topicIn, topicOut, parentMessageId, node).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <dirent.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "_runtime.h"

extern void Amalgame_Pollen_Pollen_StartListener(int64_t port);
extern void Amalgame_Pollen_Pollen_WorkflowSetSharedDir(code_string path);
extern void Amalgame_Pollen_Pollen_WorkflowSetSelf(code_string role, code_string host, int64_t port);
extern void Amalgame_Pollen_Pollen_WorkflowReloadBegin(void);
extern void Amalgame_Pollen_Pollen_WorkflowReloadCommit(void);
extern void Amalgame_Pollen_Pollen_WorkflowAddConsume(code_string topic);
extern void Amalgame_Pollen_Pollen_WorkflowAddNext(code_string host, int64_t port);
extern void Amalgame_Pollen_Pollen_WorkflowSetEmitTopic(code_string topic);
extern code_string Amalgame_Pollen_Pollen_PublishSync(code_string h, int64_t p,
                                                       code_string t, int64_t v,
                                                       code_string d, int64_t ms);

static int failures = 0, asserts = 0;
#define EXPECT_TRUE(c, l) do { asserts++; if (!(c)) { fprintf(stderr, "FAIL %s\n", (l)); failures++; } } while (0)

static int pick_port(void) {
    int s = socket(AF_INET, SOCK_STREAM, 0); if (s < 0) return 0;
    struct sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(s, (struct sockaddr*) &a, sizeof(a));
    socklen_t al = sizeof(a); getsockname(s, (struct sockaddr*) &a, &al);
    int p = ntohs(a.sin_port); close(s); return p;
}
static void msleep(int ms) { struct timespec ts; ts.tv_sec = ms/1000; ts.tv_nsec = (ms%1000)*1000000L; nanosleep(&ts, NULL); }

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
    for (;;) {
        int c = accept(srv, NULL, NULL);
        if (c < 0) break;
        char b[4096]; size_t bl = 0;
        while (bl < sizeof(b) - 1) { ssize_t n = recv(c, b+bl, sizeof(b)-1-bl, 0); if (n<=0) break; bl+=n; if (memchr(b,'\n',bl)) break; }
        b[bl]=0;
        char* mp = strstr(b, "\"messageId\":\"");
        if (mp) { mp+=13; char* mq=strchr(mp,'"'); if (mq){ char ack[256]; int al=snprintf(ack,sizeof(ack),"{\"type\":\"ACK\",\"messageId\":\"%.*s\",\"timestamp\":0}\n",(int)(mq-mp),mp); send(c,ack,al,0);} }
        close(c);
    }
    close(srv); return NULL;
}

static int node_port = 0;
static void* node_thread(void* arg) { (void) arg; Amalgame_Pollen_Pollen_StartListener((int64_t) node_port); return NULL; }

/* Read the whole executions dir, return concatenated contents +
 * count of *-ingest.json files. */
static int scan_records(const char* dir, char* out, size_t cap) {
    char path[1024]; snprintf(path, sizeof(path), "%s/executions", dir);
    DIR* d = opendir(path);
    if (!d) return 0;
    int count = 0;
    out[0] = 0;
    struct dirent* e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        count++;
        char fp[1200]; snprintf(fp, sizeof(fp), "%s/%s", path, e->d_name);
        FILE* f = fopen(fp, "r");
        if (f) {
            size_t len = strlen(out);
            size_t r = fread(out + len, 1, cap - len - 1, f);
            out[len + r] = 0;
            fclose(f);
        }
    }
    closedir(d);
    return count;
}

int main(void) {
    GC_INIT();

    const char* shared = "/tmp/test-pollen-pkg-rec";
    char rm[256]; snprintf(rm, sizeof(rm), "rm -rf '%s'", shared);
    (void) system(rm);

    node_port = pick_port();
    cap_port = pick_port();

    pthread_t cth; pthread_create(&cth, NULL, cap_thread, NULL); pthread_detach(cth);
    msleep(40);

    Amalgame_Pollen_Pollen_WorkflowSetSharedDir((code_string) shared);
    Amalgame_Pollen_Pollen_WorkflowSetSelf((code_string) "ingest", (code_string) "127.0.0.1", (int64_t) node_port);
    Amalgame_Pollen_Pollen_WorkflowReloadBegin();
    Amalgame_Pollen_Pollen_WorkflowAddConsume((code_string) "order.in");
    Amalgame_Pollen_Pollen_WorkflowSetEmitTopic((code_string) "order.enriched");
    Amalgame_Pollen_Pollen_WorkflowAddNext((code_string) "127.0.0.1", (int64_t) cap_port);
    Amalgame_Pollen_Pollen_WorkflowReloadCommit();

    pthread_t nth; pthread_create(&nth, NULL, node_thread, NULL); pthread_detach(nth);
    msleep(50);

    code_string mid = Amalgame_Pollen_Pollen_PublishSync(
        (code_string) "127.0.0.1", (int64_t) node_port,
        (code_string) "order.in", (int64_t) 1, (code_string) "{\"x\":1}", (int64_t) 2000);
    EXPECT_TRUE(mid && strlen((const char*) mid) == 36, "node ACKed publish");
    msleep(150);

    char buf[8192];
    int count = scan_records(shared, buf, sizeof(buf));
    EXPECT_TRUE(count >= 1, "at least one execution record written");
    EXPECT_TRUE(strstr(buf, "\"role\":\"ingest\"") != NULL, "record has role=ingest");
    EXPECT_TRUE(strstr(buf, "\"topicIn\":\"order.in\"") != NULL, "record topicIn=order.in");
    EXPECT_TRUE(strstr(buf, "\"topicOut\":\"order.enriched\"") != NULL, "record topicOut=order.enriched");
    EXPECT_TRUE(strstr(buf, "\"parentMessageId\":\"") != NULL, "record has parentMessageId");
    char node_needle[64]; snprintf(node_needle, sizeof(node_needle), "\"node\":\"127.0.0.1:%d\"", node_port);
    EXPECT_TRUE(strstr(buf, node_needle) != NULL, "record has node host:port");

    (void) system(rm);
    if (failures) { fprintf(stderr, "FAIL recorder smoke (%d/%d)\n", failures, asserts); return 1; }
    printf("OK recorder smoke (%d assertions)\n", asserts);
    return 0;
}
