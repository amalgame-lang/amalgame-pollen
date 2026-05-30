/* Amalgame_Pollen.h — runtime header for the amalgame-pollen
 * package. Forward declarations only ; the actual @c {} blocks
 * with the workflow engine implementation live in facade.am.
 *
 * Consumers : Mosaic web apps that embed Pollen, or any
 * Amalgame program that needs the v3 workflow dispatcher.
 * They import the package via :
 *
 *   import Amalgame.Pollen
 *
 * which makes the symbols below visible after amc has emitted
 * the #include + linked against the package archive.
 *
 * v0.2.0 — the v1/v2 workflow surface (WorkflowLoad,
 * WorkflowReload* / AddConsume / AddNext / SetEmitTopic /
 * CondBranch* / For* / While* / SetOpAdd, StateGet / StateSet,
 * EvalExpr / EvalCond, PublishDebug, OnMessage / OnComplete /
 * Forward, WorkflowVersion) was retired with the v2 dispatcher.
 * Workflows now describe themselves with the v3 schema
 * (`{schema: "pollen/v3", actions, entries}`) and dispatch
 * happens via the WorkflowV3* entry points below.
 */

#ifndef AMALGAME_POLLEN_H
#define AMALGAME_POLLEN_H

/* _runtime.h provides code_string / code_bool + the GC shims. The
 * earlier "Amalgame.h" include was a stray — it doesn't exist in the
 * amc runtime dir, which broke any CONSUMER whose generated C pulls
 * in this header (the package's own --lib build never tripped it
 * because facade.c includes _runtime.h directly). */
#include "_runtime.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The engine is a singleton-per-process : one workflow loaded at a
 * time, one capability registry, one bus listener. v0.3 may
 * introduce an opaque AmalgamePollenEngine handle for multi-engine
 * hosting (e.g. a single Mosaic app running two independent
 * workflows on different port ranges). */

/* === Shared infrastructure ====================================== */

/* Configure sharedDir/state/ + sharedDir/executions/ + sharedDir/
 * capabilities/ location. */
void        Amalgame_Pollen_Pollen_WorkflowSetSharedDir(code_string path);
/* Self identity for the executions/ recorder + capability ad. */
void        Amalgame_Pollen_Pollen_WorkflowSetSelf(code_string role, code_string host, int64_t port);
/* Returns the role set by WorkflowSetSelf (or ""). */
code_string Amalgame_Pollen_Pollen_WorkflowActiveRole(void);

/* TCP transport */
void        Amalgame_Pollen_Pollen_StartListener(int64_t port);
code_string Amalgame_Pollen_Pollen_Publish(code_string host, int64_t port,
                                            code_string topicUuid, int64_t topicVersion,
                                            code_string dataJson);
code_string Amalgame_Pollen_Pollen_PublishSync(code_string host, int64_t port,
                                                code_string topicUuid, int64_t topicVersion,
                                                code_string dataJson, int64_t timeoutMs);

/* Capability discovery (Phase 6.1) + power-of-two LB (Phase 6.3).
 * StartCapabilityWriter advertises this node's actions ; the reader
 * builds an in-memory registry of live providers ; SetLoadBalance
 * toggles registry-resolved forwarding ; RegistrySize +
 * ResolveProvider are introspection. */
void        Amalgame_Pollen_Pollen_StartCapabilityWriter(code_string label, code_string host, int64_t port);
void        Amalgame_Pollen_Pollen_StartCapabilityReader(void);
void        Amalgame_Pollen_Pollen_SetLoadBalance(code_bool on);
int64_t     Amalgame_Pollen_Pollen_RegistrySize(void);
code_string Amalgame_Pollen_Pollen_ResolveProvider(code_string topic);

/* === Pollen v3 — workflow validator + loader =================== */

/* Returns a List<string> of diagnostics ("[severity ruleN] path:
 * message"). Empty list = clean v3 workflow.json. */
void*       Amalgame_Pollen_Pollen_WorkflowValidateV3(code_string workflowPath);
/* Load + resolve a v3 workflow into the runtime AST pools. Returns
 * true on success ; WorkflowV3LoadErrorMsg holds the last error. */
code_bool   Amalgame_Pollen_Pollen_WorkflowLoadV3(code_string workflowPath);
code_string Amalgame_Pollen_Pollen_WorkflowV3LoadErrorMsg(void);

/* === Pollen v3 — introspection (tests + manager) ============== */

code_bool   Amalgame_Pollen_Pollen_WorkflowV3IsActive(void);
int64_t     Amalgame_Pollen_Pollen_WorkflowV3EntryCount(void);
int64_t     Amalgame_Pollen_Pollen_WorkflowV3AnchorCount(void);
int64_t     Amalgame_Pollen_Pollen_WorkflowV3ActionCount(void);
int64_t     Amalgame_Pollen_Pollen_WorkflowV3NodeCount(void);
code_string Amalgame_Pollen_Pollen_WorkflowV3EntryName(int64_t eidx);
int64_t     Amalgame_Pollen_Pollen_WorkflowV3EntryDoRoot(int64_t eidx);
int64_t     Amalgame_Pollen_Pollen_WorkflowV3NodeKind(int64_t idx);
int64_t     Amalgame_Pollen_Pollen_WorkflowV3NodeChild0(int64_t idx);
int64_t     Amalgame_Pollen_Pollen_WorkflowV3NodeNext(int64_t idx);
int64_t     Amalgame_Pollen_Pollen_WorkflowV3NodeGotoKind(int64_t idx);
int64_t     Amalgame_Pollen_Pollen_WorkflowV3NodeGotoIdx(int64_t idx);
code_string Amalgame_Pollen_Pollen_WorkflowV3NodeName(int64_t idx);
code_string Amalgame_Pollen_Pollen_WorkflowV3NodeExpr(int64_t idx);
code_string Amalgame_Pollen_Pollen_WorkflowV3NodeBind(int64_t idx);
int64_t     Amalgame_Pollen_Pollen_WorkflowV3NodeMode(int64_t idx);
int64_t     Amalgame_Pollen_Pollen_WorkflowV3NodeOnError(int64_t idx);
int64_t     Amalgame_Pollen_Pollen_WorkflowV3NodeMaxIter(int64_t idx);

/* === Pollen v3 — dispatcher entry points ====================== */

/* Dispatch a workflow entry by index. Returns 0 on OK, non-zero on
 * abort/error. The *State variant returns the final state blackboard
 * as a JsonValue for tests + introspection. */
int64_t     Amalgame_Pollen_Pollen_WorkflowV3DispatchEntry(int64_t eidx, code_string envelopeJson);
void*       Amalgame_Pollen_Pollen_WorkflowV3DispatchEntryState(int64_t eidx, code_string envelopeJson);

/* Resolve a bus topic to an entry index (or -1 if no entry consumes
 * it). DispatchTopic is the listener fast-path. */
int64_t     Amalgame_Pollen_Pollen_WorkflowV3LookupEntryByTopic(code_string topic);
int64_t     Amalgame_Pollen_Pollen_WorkflowV3DispatchTopic(code_string topic, code_string envelopeJson);
void*       Amalgame_Pollen_Pollen_WorkflowV3DispatchTopicState(code_string topic, code_string envelopeJson);

/* Atomic counter bumped each time the listener fork routes to v3.
 * Reset variant for tests that need a clean baseline. */
int64_t     Amalgame_Pollen_Pollen_WorkflowV3DispatchCount(void);
void        Amalgame_Pollen_Pollen_WorkflowV3ResetDispatchCount(void);

#ifdef __cplusplus
}
#endif

#endif /* AMALGAME_POLLEN_H */
