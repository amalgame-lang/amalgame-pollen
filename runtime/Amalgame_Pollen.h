/* Amalgame_Pollen.h — runtime header for the amalgame-pollen
 * package. Forward declarations only ; the actual @c {} blocks
 * with the workflow engine implementation live in facade.am
 * (singleton-per-process design, same as pollen-node-tcp.am
 * v0.2).
 *
 * Consumers : the pollen CLI binary, Mosaic web apps that embed
 * Pollen, or any Amalgame program that needs the workflow-tree
 * runtime. They import the package via :
 *
 *   import Amalgame.Pollen
 *
 * which makes the symbols below visible after amc has emitted
 * the #include + linked against the package archive.
 */

#ifndef AMALGAME_POLLEN_H
#define AMALGAME_POLLEN_H

#include "Amalgame.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The engine is presently a singleton-per-process : v0.1 keeps
 * the same static-global storage used in pollen-node-tcp.am for
 * compat. v0.2 will introduce an opaque AmalgamePollenEngine
 * handle for multi-engine hosting (e.g. a single Mosaic app
 * running two independent workflows on different port ranges). */

/* Workflow loading + lifecycle */
code_bool   Amalgame_Pollen_Pollen_WorkflowLoad(code_string path, code_string nodeName, int64_t actualPort);
void        Amalgame_Pollen_Pollen_WorkflowSetSharedDir(code_string path);
void        Amalgame_Pollen_Pollen_WorkflowSetSelf(code_string role, code_string host, int64_t port);

/* M2.3c.1 — workflow runtime setters (consumes / nexts / emit topic).
 * Bracket the AddConsume/AddNext/SetEmitTopic calls with the
 * ReloadBegin / ReloadCommit pair — they hold a mutex against the
 * listener dispatch. */
void        Amalgame_Pollen_Pollen_WorkflowReloadBegin(void);
void        Amalgame_Pollen_Pollen_WorkflowReloadCommit(void);
void        Amalgame_Pollen_Pollen_WorkflowAddConsume(code_string topic);
void        Amalgame_Pollen_Pollen_WorkflowAddNext(code_string host, int64_t port);
void        Amalgame_Pollen_Pollen_WorkflowSetEmitTopic(code_string topic);

/* M2.3c.2 — cond branches (Phase 5.2 `if`) + set state.X ops (Phase
 * 5.3 `set`). Both fit between ReloadBegin/ReloadCommit alongside
 * the flat-topology setters. */
void        Amalgame_Pollen_Pollen_CondBranchOpen(code_string condJson);
void        Amalgame_Pollen_Pollen_CondBranchAddTarget(code_string host, int64_t port);
void        Amalgame_Pollen_Pollen_SetOpAdd(code_string path, code_string valueExpr);

/* M2.3c.2b — for / while loops (Phase 5.4). */
void        Amalgame_Pollen_Pollen_ForSetup(code_string itemVar);
void        Amalgame_Pollen_Pollen_ForAddTarget(code_string host, int64_t port);
void        Amalgame_Pollen_Pollen_ForAddItem(code_string itemLit);
void        Amalgame_Pollen_Pollen_WhileSetup(code_string condJson,
                                                code_string iterKey,
                                                int64_t maxIter,
                                                code_string selfHost,
                                                int64_t selfPort);
void        Amalgame_Pollen_Pollen_WhileAddExit(code_string host, int64_t port);

/* Phase 5.3 — per-execution state file under sharedDir/state/<rootMid>.json.
 * StateGet returns the raw JSON literal for the key (or "" if absent).
 * StateSet replace-or-append the key, atomic tmp+rename. */
code_string Amalgame_Pollen_Pollen_StateGet(code_string rootMid, code_string path);
code_bool   Amalgame_Pollen_Pollen_StateSet(code_string rootMid, code_string path, code_string jsonLiteral);

/* M2.2 — expression + cond evaluators used by workflow-tree set/if.
 * EvalExpr returns the literal form of the evaluated value.
 * EvalCond evaluates a leaf / composite (and/or/not) / membership
 * (in/not_in) condition against the envelope. */
code_string Amalgame_Pollen_Pollen_EvalExpr(code_string envelopeJson, code_string exprJson);
code_bool   Amalgame_Pollen_Pollen_EvalCond(code_string envelopeJson, code_string condJson);

/* TCP transport */
void        Amalgame_Pollen_Pollen_StartListener(int64_t port);
code_string Amalgame_Pollen_Pollen_Publish(code_string host, int64_t port,
                                            code_string topicUuid, int64_t topicVersion,
                                            code_string dataJson);
code_string Amalgame_Pollen_Pollen_PublishSync(code_string host, int64_t port,
                                                code_string topicUuid, int64_t topicVersion,
                                                code_string dataJson, int64_t timeoutMs);

/* Introspection */
int64_t     Amalgame_Pollen_Pollen_WorkflowVersion(void);
code_string Amalgame_Pollen_Pollen_WorkflowActiveRole(void);

#ifdef __cplusplus
}
#endif

#endif /* AMALGAME_POLLEN_H */
