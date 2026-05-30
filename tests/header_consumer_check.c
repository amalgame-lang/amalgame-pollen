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
 *
 * v0.2.0 — the v2 workflow surface (WorkflowLoad / WorkflowReload*
 * / CondBranch* / For* / While* / SetOpAdd / StateGet / StateSet /
 * EvalExpr / EvalCond / PublishDebug / OnMessage / OnComplete /
 * Forward / WorkflowVersion) was retired with the v2 dispatcher.
 * The list below covers the v3 dispatcher + the shared TCP / cap-
 * registry infrastructure that survived the cut.
 */

#include "_runtime.h"
#include "Amalgame_Pollen.h"
#include <stdio.h>

int main(void) {
    /* Take the address of each public entry point — proves the
     * header declarations exist + are well-formed. Not called. */
    void* fns[] = {
        /* Shared infrastructure */
        (void*) &Amalgame_Pollen_Pollen_WorkflowSetSharedDir,
        (void*) &Amalgame_Pollen_Pollen_WorkflowSetSelf,
        (void*) &Amalgame_Pollen_Pollen_WorkflowActiveRole,
        (void*) &Amalgame_Pollen_Pollen_StartListener,
        (void*) &Amalgame_Pollen_Pollen_StartCapabilityWriter,
        (void*) &Amalgame_Pollen_Pollen_StartCapabilityReader,
        (void*) &Amalgame_Pollen_Pollen_SetLoadBalance,
        (void*) &Amalgame_Pollen_Pollen_RegistrySize,
        (void*) &Amalgame_Pollen_Pollen_ResolveProvider,
        (void*) &Amalgame_Pollen_Pollen_Publish,
        (void*) &Amalgame_Pollen_Pollen_PublishSync,
        /* v3 validator + loader */
        (void*) &Amalgame_Pollen_Pollen_WorkflowValidateV3,
        (void*) &Amalgame_Pollen_Pollen_WorkflowLoadV3,
        (void*) &Amalgame_Pollen_Pollen_WorkflowV3LoadErrorMsg,
        /* v3 introspection */
        (void*) &Amalgame_Pollen_Pollen_WorkflowV3IsActive,
        (void*) &Amalgame_Pollen_Pollen_WorkflowV3EntryCount,
        (void*) &Amalgame_Pollen_Pollen_WorkflowV3AnchorCount,
        (void*) &Amalgame_Pollen_Pollen_WorkflowV3ActionCount,
        (void*) &Amalgame_Pollen_Pollen_WorkflowV3NodeCount,
        (void*) &Amalgame_Pollen_Pollen_WorkflowV3EntryName,
        (void*) &Amalgame_Pollen_Pollen_WorkflowV3EntryDoRoot,
        (void*) &Amalgame_Pollen_Pollen_WorkflowV3NodeKind,
        (void*) &Amalgame_Pollen_Pollen_WorkflowV3NodeChild0,
        (void*) &Amalgame_Pollen_Pollen_WorkflowV3NodeNext,
        (void*) &Amalgame_Pollen_Pollen_WorkflowV3NodeGotoKind,
        (void*) &Amalgame_Pollen_Pollen_WorkflowV3NodeGotoIdx,
        (void*) &Amalgame_Pollen_Pollen_WorkflowV3NodeName,
        (void*) &Amalgame_Pollen_Pollen_WorkflowV3NodeExpr,
        (void*) &Amalgame_Pollen_Pollen_WorkflowV3NodeBind,
        (void*) &Amalgame_Pollen_Pollen_WorkflowV3NodeMode,
        (void*) &Amalgame_Pollen_Pollen_WorkflowV3NodeOnError,
        (void*) &Amalgame_Pollen_Pollen_WorkflowV3NodeMaxIter,
        /* v3 dispatcher entry points */
        (void*) &Amalgame_Pollen_Pollen_WorkflowV3DispatchEntry,
        (void*) &Amalgame_Pollen_Pollen_WorkflowV3DispatchEntryState,
        (void*) &Amalgame_Pollen_Pollen_WorkflowV3LookupEntryByTopic,
        (void*) &Amalgame_Pollen_Pollen_WorkflowV3DispatchTopic,
        (void*) &Amalgame_Pollen_Pollen_WorkflowV3DispatchTopicState,
        (void*) &Amalgame_Pollen_Pollen_WorkflowV3DispatchCount,
        (void*) &Amalgame_Pollen_Pollen_WorkflowV3ResetDispatchCount,
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
