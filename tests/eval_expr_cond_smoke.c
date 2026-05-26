/* eval_expr_cond_smoke.c — exercises the v0.1.3 expr + cond
 * evaluators (M2.2). Builds against the package archive directly.
 *
 * Coverage :
 *   - const literal (number + string)
 *   - var lookup against envelope (data.X)
 *   - var lookup against state file (state.X)
 *   - arithmetic op : +, -, *, /
 *   - cond leaf ops : == != < > <= >=
 *   - cond composite : and / or / not
 *   - cond membership : in / not_in
 *   - else / empty cond → true
 *   - missing var fallback → numeric 0 (eval) / fail (cond)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "_runtime.h"

extern void        Amalgame_Pollen_Pollen_WorkflowSetSharedDir(code_string path);
extern code_bool   Amalgame_Pollen_Pollen_StateSet(code_string rootMid,
                                                     code_string path,
                                                     code_string jsonLiteral);
extern code_string Amalgame_Pollen_Pollen_EvalExpr(code_string envelopeJson,
                                                     code_string exprJson);
extern code_bool   Amalgame_Pollen_Pollen_EvalCond(code_string envelopeJson,
                                                     code_string condJson);

static int failures = 0;
static int asserts  = 0;

#define EXPECT_STREQ(actual, expected, label) do {                             \
    asserts++;                                                                 \
    if (strcmp((const char*) (actual), (expected)) != 0) {                     \
        fprintf(stderr, "FAIL %s : expected \"%s\" got \"%s\"\n",              \
                (label), (expected), (const char*) (actual));                  \
        failures++;                                                            \
    }                                                                          \
} while (0)

#define EXPECT_TRUE(cond, label) do {                                          \
    asserts++;                                                                 \
    if (!(cond)) {                                                             \
        fprintf(stderr, "FAIL %s : expected true got false\n", (label));       \
        failures++;                                                            \
    }                                                                          \
} while (0)

#define EXPECT_FALSE(cond, label) do {                                         \
    asserts++;                                                                 \
    if ((cond)) {                                                              \
        fprintf(stderr, "FAIL %s : expected false got true\n", (label));       \
        failures++;                                                            \
    }                                                                          \
} while (0)

static code_bool eval_cond(const char* env, const char* cond) {
    return Amalgame_Pollen_Pollen_EvalCond((code_string) env, (code_string) cond);
}
static code_string eval_expr(const char* env, const char* expr) {
    return Amalgame_Pollen_Pollen_EvalExpr((code_string) env, (code_string) expr);
}

int main(void) {
    GC_INIT();

    /* Sample envelope — includes rootMessageId so state lookups have a
     * mid to anchor on, plus a nested data object for dotted paths. */
    const char* env =
        "{\"messageId\":\"m-1\","
         "\"rootMessageId\":\"root-abc\","
         "\"data\":{\"user\":{\"tier\":\"vip\",\"age\":30},"
                   "\"amount\":42,\"label\":\"hello\"}}";

    /* === expr evaluator ===================================== */
    EXPECT_STREQ(eval_expr(env, "{\"const\":7}"),         "7",       "const num");
    EXPECT_STREQ(eval_expr(env, "{\"const\":\"hi\"}"),    "\"hi\"",  "const str");
    EXPECT_STREQ(eval_expr(env, "{\"var\":\"data.amount\"}"), "42",  "var data.amount");
    EXPECT_STREQ(eval_expr(env, "{\"var\":\"data.label\"}"),
                 "\"hello\"", "var data.label");
    EXPECT_STREQ(eval_expr(env, "{\"var\":\"data.user.age\"}"),
                 "30", "var nested");

    EXPECT_STREQ(eval_expr(env,
                  "{\"op\":\"+\",\"left\":{\"const\":2},\"right\":{\"const\":3}}"),
                 "5", "op add");
    EXPECT_STREQ(eval_expr(env,
                  "{\"op\":\"*\",\"left\":{\"var\":\"data.amount\"},\"right\":{\"const\":2}}"),
                 "84", "op mul var+const");
    EXPECT_STREQ(eval_expr(env,
                  "{\"op\":\"/\",\"left\":{\"const\":10},\"right\":{\"const\":0}}"),
                 "0", "div by zero → 0");

    /* Missing var → numeric 0 (counter-init pattern). */
    EXPECT_STREQ(eval_expr(env, "{\"var\":\"data.absent\"}"), "0", "missing var → 0");

    /* === state.X path ======================================= */
    const char* shared = "/tmp/test-pollen-pkg-eval";
    char rmcmd[256]; snprintf(rmcmd, sizeof(rmcmd), "rm -rf '%s'", shared);
    (void) system(rmcmd);
    Amalgame_Pollen_Pollen_WorkflowSetSharedDir((code_string) shared);
    Amalgame_Pollen_Pollen_StateSet((code_string) "root-abc",
                                      (code_string) "state.counter",
                                      (code_string) "10");

    EXPECT_STREQ(eval_expr(env, "{\"var\":\"state.counter\"}"), "10",
                 "var state.counter");
    EXPECT_STREQ(eval_expr(env,
                  "{\"op\":\"+\",\"left\":{\"var\":\"state.counter\"},\"right\":{\"const\":1}}"),
                 "11", "counter increment");

    /* === cond evaluator ===================================== */
    /* Empty / else branch → true */
    EXPECT_TRUE(eval_cond(env, ""), "else branch");

    /* Leaf : numeric ops */
    EXPECT_TRUE(eval_cond(env,
        "{\"op\":\"==\",\"var\":\"data.amount\",\"value\":42}"), "eq num");
    EXPECT_FALSE(eval_cond(env,
        "{\"op\":\"==\",\"var\":\"data.amount\",\"value\":7}"), "eq num neg");
    EXPECT_TRUE(eval_cond(env,
        "{\"op\":\">\",\"var\":\"data.amount\",\"value\":10}"), "gt");
    EXPECT_TRUE(eval_cond(env,
        "{\"op\":\"<=\",\"var\":\"data.amount\",\"value\":42}"), "le");

    /* Leaf : string ops */
    EXPECT_TRUE(eval_cond(env,
        "{\"op\":\"==\",\"var\":\"data.label\",\"value\":\"hello\"}"), "eq str");
    EXPECT_FALSE(eval_cond(env,
        "{\"op\":\"!=\",\"var\":\"data.label\",\"value\":\"hello\"}"), "ne str neg");

    /* Composite : and / or / not */
    EXPECT_TRUE(eval_cond(env,
        "{\"op\":\"and\",\"args\":["
          "{\"op\":\">\",\"var\":\"data.amount\",\"value\":10},"
          "{\"op\":\"==\",\"var\":\"data.user.tier\",\"value\":\"vip\"}"
        "]}"), "and");
    EXPECT_FALSE(eval_cond(env,
        "{\"op\":\"and\",\"args\":["
          "{\"op\":\">\",\"var\":\"data.amount\",\"value\":1000},"
          "{\"op\":\"==\",\"var\":\"data.user.tier\",\"value\":\"vip\"}"
        "]}"), "and false");
    EXPECT_TRUE(eval_cond(env,
        "{\"op\":\"or\",\"args\":["
          "{\"op\":\"==\",\"var\":\"data.user.tier\",\"value\":\"gold\"},"
          "{\"op\":\"==\",\"var\":\"data.user.tier\",\"value\":\"vip\"}"
        "]}"), "or");
    EXPECT_TRUE(eval_cond(env,
        "{\"op\":\"not\",\"arg\":"
          "{\"op\":\"==\",\"var\":\"data.amount\",\"value\":7}"
        "}"), "not");

    /* Membership : in / not_in */
    EXPECT_TRUE(eval_cond(env,
        "{\"op\":\"in\",\"var\":\"data.user.tier\","
         "\"values\":[\"vip\",\"gold\"]}"), "in match");
    EXPECT_FALSE(eval_cond(env,
        "{\"op\":\"in\",\"var\":\"data.user.tier\","
         "\"values\":[\"bronze\",\"silver\"]}"), "in miss");
    EXPECT_TRUE(eval_cond(env,
        "{\"op\":\"not_in\",\"var\":\"data.user.tier\","
         "\"values\":[\"bronze\"]}"), "not_in match");

    /* state.X in cond */
    EXPECT_TRUE(eval_cond(env,
        "{\"op\":\"==\",\"var\":\"state.counter\",\"value\":10}"), "state cond eq");
    EXPECT_FALSE(eval_cond(env,
        "{\"op\":\"==\",\"var\":\"state.counter\",\"value\":11}"), "state cond neg");

    /* Missing var in cond → false (no match) */
    EXPECT_FALSE(eval_cond(env,
        "{\"op\":\"==\",\"var\":\"data.absent\",\"value\":1}"), "cond missing var");

    (void) system(rmcmd);

    if (failures != 0) {
        fprintf(stderr, "FAIL eval_expr_cond smoke (%d / %d failed)\n",
                failures, asserts);
        return 1;
    }
    printf("OK eval_expr_cond smoke (%d assertions)\n", asserts);
    return 0;
}
