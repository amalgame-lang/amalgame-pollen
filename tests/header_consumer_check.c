/* header_consumer_check.c — compile-time guard that the package's
 * public runtime header is includable by a CONSUMER.
 *
 * This is the test that would have caught the v0.1.10 bug : the
 * header used to `#include "Amalgame.h"` (no such file), which the
 * package's own --lib build never tripped but every consumer did.
 *
 * It includes the header exactly as a consumer's amc-generated C
 * does, references each declared symbol's address (forcing the
 * decls to type-check), and exits 0. Linked against the archive by
 * run_tests.sh so the symbols resolve too.
 */

#include "_runtime.h"
#include "Amalgame_Pollen.h"
#include <stdio.h>

int main(void) {
    /* Take the address of each public entry point — proves the
     * header declarations exist + are well-formed. Not called. */
    void* fns[] = {
        (void*) &Amalgame_Pollen_Pollen_WorkflowLoad,
        (void*) &Amalgame_Pollen_Pollen_WorkflowSetSharedDir,
        (void*) &Amalgame_Pollen_Pollen_WorkflowSetSelf,
        (void*) &Amalgame_Pollen_Pollen_StateGet,
        (void*) &Amalgame_Pollen_Pollen_StateSet,
        (void*) &Amalgame_Pollen_Pollen_EvalExpr,
        (void*) &Amalgame_Pollen_Pollen_EvalCond,
        (void*) &Amalgame_Pollen_Pollen_Publish,
        (void*) &Amalgame_Pollen_Pollen_PublishSync,
        (void*) &Amalgame_Pollen_Pollen_PublishDebug,
        (void*) &Amalgame_Pollen_Pollen_StartListener,
        (void*) &Amalgame_Pollen_Pollen_WorkflowReloadBegin,
        (void*) &Amalgame_Pollen_Pollen_WorkflowReloadCommit,
        (void*) &Amalgame_Pollen_Pollen_WorkflowAddConsume,
        (void*) &Amalgame_Pollen_Pollen_WorkflowAddNext,
        (void*) &Amalgame_Pollen_Pollen_WorkflowSetEmitTopic,
        (void*) &Amalgame_Pollen_Pollen_CondBranchOpen,
        (void*) &Amalgame_Pollen_Pollen_CondBranchAddTarget,
        (void*) &Amalgame_Pollen_Pollen_CondBranchSetTopic,
        (void*) &Amalgame_Pollen_Pollen_SetOpAdd,
        (void*) &Amalgame_Pollen_Pollen_ForSetup,
        (void*) &Amalgame_Pollen_Pollen_ForAddTarget,
        (void*) &Amalgame_Pollen_Pollen_ForAddItem,
        (void*) &Amalgame_Pollen_Pollen_WhileSetup,
        (void*) &Amalgame_Pollen_Pollen_WhileAddExit,
        (void*) &Amalgame_Pollen_Pollen_OnMessage,
        (void*) &Amalgame_Pollen_Pollen_OnComplete,
        (void*) &Amalgame_Pollen_Pollen_Forward,
        (void*) &Amalgame_Pollen_Pollen_StartCapabilityReader,
        (void*) &Amalgame_Pollen_Pollen_SetLoadBalance,
        (void*) &Amalgame_Pollen_Pollen_RegistrySize,
        (void*) &Amalgame_Pollen_Pollen_ResolveProvider,
        (void*) &Amalgame_Pollen_Pollen_WorkflowVersion,
        (void*) &Amalgame_Pollen_Pollen_WorkflowActiveRole,
    };
    int n = (int)(sizeof(fns) / sizeof(fns[0]));
    int nonnull = 0;
    for (int i = 0; i < n; i++) if (fns[i]) nonnull++;
    if (nonnull != n) {
        fprintf(stderr, "FAIL header check : %d/%d symbols null\n", nonnull, n);
        return 1;
    }
    printf("OK header consumer check (%d public symbols declared)\n", n);
    return 0;
}
