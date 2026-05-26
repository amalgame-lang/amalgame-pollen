/* state_persist_smoke.c — pure C smoke test for the StateGet /
 * StateSet pair shipped in v0.1.2 (M2.1 migration).
 *
 * Bypasses amc — links straight against the libamalgame-pkg-Pollen.a
 * archive, since spinning up a full package consumer build would
 * require the package to be resolved through packages-index which
 * is a chicken-and-egg before tagging.
 *
 * Build (from repo root) :
 *
 *   amc --lib --quiet facade.am -o /tmp/Pollen-facade
 *   gcc -O2 -I<amc-runtime> -I./runtime -c /tmp/Pollen-facade.c -o /tmp/Pollen-facade.o
 *   ar rcs build/linux-x86_64/libamalgame-pkg-Pollen.a /tmp/Pollen-facade.o
 *   gcc -O2 -I<amc-runtime> -I./runtime tests/state_persist_smoke.c \
 *       build/linux-x86_64/libamalgame-pkg-Pollen.a \
 *       -lgc -lpthread -o /tmp/state_persist_smoke
 *   /tmp/state_persist_smoke
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "_runtime.h"

/* Forward decls — Amalgame_Pollen.h pulls in transitively-derived
 * "Amalgame.h" which is currently an amc-emit convention, not a
 * real header on disk. Smoke-link declares the package symbols
 * directly instead. */
extern code_bool   Amalgame_Pollen_Pollen_StateSet(code_string rootMid,
                                                     code_string path,
                                                     code_string jsonLiteral);
extern code_string Amalgame_Pollen_Pollen_StateGet(code_string rootMid,
                                                     code_string path);
extern void        Amalgame_Pollen_Pollen_WorkflowSetSharedDir(code_string path);

#define ASSERT_STREQ(actual, expected, label)                                  \
    do {                                                                       \
        if (strcmp((const char*) (actual), (expected)) != 0) {                 \
            fprintf(stderr, "FAIL %s : expected %s got %s\n",                  \
                    (label), (expected), (const char*) (actual));              \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

static void rm_rf(const char* path) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", path);
    (void) system(cmd);
}

int main(void) {
    GC_INIT();

    const char* dir = "/tmp/test-pollen-pkg-state";
    rm_rf(dir);

    Amalgame_Pollen_Pollen_WorkflowSetSharedDir((code_string) dir);

    /* 1. fresh write — counter=1 */
    code_bool ok = Amalgame_Pollen_Pollen_StateSet(
        (code_string) "abc", (code_string) "state.counter", (code_string) "1");
    if (!ok) { fprintf(stderr, "FAIL : StateSet returned false\n"); return 1; }

    code_string got = Amalgame_Pollen_Pollen_StateGet(
        (code_string) "abc", (code_string) "state.counter");
    ASSERT_STREQ(got, "1", "StateGet(counter)");

    /* 2. replace — counter=42 */
    ok = Amalgame_Pollen_Pollen_StateSet(
        (code_string) "abc", (code_string) "state.counter", (code_string) "42");
    if (!ok) { fprintf(stderr, "FAIL : StateSet replace returned false\n"); return 1; }
    got = Amalgame_Pollen_Pollen_StateGet(
        (code_string) "abc", (code_string) "state.counter");
    ASSERT_STREQ(got, "42", "StateGet(counter after replace)");

    /* 3. append — name="alice" alongside counter */
    ok = Amalgame_Pollen_Pollen_StateSet(
        (code_string) "abc", (code_string) "state.name", (code_string) "\"alice\"");
    if (!ok) { fprintf(stderr, "FAIL : StateSet append returned false\n"); return 1; }
    got = Amalgame_Pollen_Pollen_StateGet(
        (code_string) "abc", (code_string) "state.name");
    ASSERT_STREQ(got, "\"alice\"", "StateGet(name)");
    got = Amalgame_Pollen_Pollen_StateGet(
        (code_string) "abc", (code_string) "state.counter");
    ASSERT_STREQ(got, "42", "StateGet(counter after append)");

    /* 4. missing key → "" */
    got = Amalgame_Pollen_Pollen_StateGet(
        (code_string) "abc", (code_string) "state.missing");
    ASSERT_STREQ(got, "", "StateGet(missing)");

    /* 5. missing file → "" */
    got = Amalgame_Pollen_Pollen_StateGet(
        (code_string) "def", (code_string) "state.counter");
    ASSERT_STREQ(got, "", "StateGet(missing file)");

    /* 6. path without "state." prefix works (bare key) */
    ok = Amalgame_Pollen_Pollen_StateSet(
        (code_string) "abc", (code_string) "bare", (code_string) "true");
    if (!ok) { fprintf(stderr, "FAIL : StateSet bare returned false\n"); return 1; }
    got = Amalgame_Pollen_Pollen_StateGet(
        (code_string) "abc", (code_string) "bare");
    ASSERT_STREQ(got, "true", "StateGet(bare)");

    /* 7. file was created at sharedDir/state/<root>.json */
    struct stat st;
    char path[1024];
    snprintf(path, sizeof(path), "%s/state/abc.json", dir);
    if (stat(path, &st) != 0) {
        fprintf(stderr, "FAIL : state file %s missing\n", path);
        return 1;
    }

    rm_rf(dir);
    printf("OK state_persist smoke (7 assertions)\n");
    return 0;
}
