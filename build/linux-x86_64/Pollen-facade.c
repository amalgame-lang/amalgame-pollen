#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "_runtime.h"
#include "Amalgame_String.h"
#include "Amalgame_Collections.h"
#include "Amalgame_IO.h"
#include "Amalgame_Net.h"
#include "Amalgame_Console.h"
#include "Amalgame_Process.h"

/* inline-C top-level */

    #include <stdio.h>
    #include <stdint.h>
    #include <string.h>
    #include <gc.h>

    /* Singleton-per-process engine state. v0.1 carries flags only ;
     * M2-M3 will copy in the full runtime from pollen.am. */
    static int _pollen_pkg_loaded = 0;
    static int _pollen_pkg_version = 0;
    static char _pollen_pkg_role[64] = "";
    static char _pollen_pkg_shared_dir[512] = "";

    static const char _pollen_pkg_unimpl_warning[] =
        "amalgame-pollen v0.1.0-dev : %s is a stub. "
        "M2-M3 migration from pollen.am in progress.\n";

    /* === Workflow lifecycle =================================== */
    static code_bool
    Amalgame_Pollen_Pollen_WorkflowLoad_c(code_string path, code_string nodeName,
                                            int64_t actualPort) {
        fprintf(stderr, _pollen_pkg_unimpl_warning, "WorkflowLoad");
        (void) path; (void) nodeName; (void) actualPort;
        return (code_bool) 0;
    }

    static void
    Amalgame_Pollen_Pollen_WorkflowSetSharedDir_c(code_string path) {
        if (path) {
            strncpy(_pollen_pkg_shared_dir, path,
                     sizeof(_pollen_pkg_shared_dir) - 1);
            _pollen_pkg_shared_dir[sizeof(_pollen_pkg_shared_dir) - 1] = 0;
        }
    }

    static void
    Amalgame_Pollen_Pollen_WorkflowSetSelf_c(code_string role, code_string host,
                                               int64_t port) {
        if (role) {
            strncpy(_pollen_pkg_role, role, sizeof(_pollen_pkg_role) - 1);
            _pollen_pkg_role[sizeof(_pollen_pkg_role) - 1] = 0;
        }
        (void) host; (void) port;
    }

    /* === TCP transport ======================================== */
    static void
    Amalgame_Pollen_Pollen_StartListener_c(int64_t port) {
        fprintf(stderr, _pollen_pkg_unimpl_warning, "StartListener");
        (void) port;
    }

    static code_string
    Amalgame_Pollen_Pollen_Publish_c(code_string host, int64_t port,
                                       code_string topicUuid, int64_t topicVersion,
                                       code_string dataJson) {
        fprintf(stderr, _pollen_pkg_unimpl_warning, "Publish");
        (void) host; (void) port; (void) topicUuid;
        (void) topicVersion; (void) dataJson;
        /* Return an empty string allocated through bdwgc so the
         * caller can hold onto it without a use-after-free. */
        char* out = (char*) GC_MALLOC_ATOMIC(1);
        if (out) out[0] = 0;
        return (code_string) out;
    }

    /* === Introspection ======================================== */
    static int64_t
    Amalgame_Pollen_Pollen_WorkflowVersion_c(void) {
        return (int64_t) _pollen_pkg_version;
    }

    static code_string
    Amalgame_Pollen_Pollen_WorkflowActiveRole_c(void) {
        size_t len = strlen(_pollen_pkg_role);
        char* out = (char*) GC_MALLOC_ATOMIC(len + 1);
        if (out) { memcpy(out, _pollen_pkg_role, len); out[len] = 0; }
        return (code_string) out;
    }

typedef struct _Amalgame_Pollen_Pollen Amalgame_Pollen_Pollen;

Amalgame_Pollen_Pollen* Amalgame_Pollen_Pollen_new();
code_bool Amalgame_Pollen_Pollen_WorkflowLoad(code_string workflowPath, code_string nodeName, i64 actualPort);
void Amalgame_Pollen_Pollen_WorkflowSetSharedDir(code_string path);
void Amalgame_Pollen_Pollen_WorkflowSetSelf(code_string role, code_string host, i64 port);
void Amalgame_Pollen_Pollen_StartListener(i64 port);
code_string Amalgame_Pollen_Pollen_Publish(code_string host, i64 port, code_string topicUuid, i64 topicVersion, code_string dataJson);
i64 Amalgame_Pollen_Pollen_WorkflowVersion();
code_string Amalgame_Pollen_Pollen_WorkflowActiveRole();
struct _Amalgame_Pollen_Pollen {
};

code_bool Amalgame_Pollen_Pollen_WorkflowLoad(code_string workflowPath, code_string nodeName, i64 actualPort);
void Amalgame_Pollen_Pollen_WorkflowSetSharedDir(code_string path);
void Amalgame_Pollen_Pollen_WorkflowSetSelf(code_string role, code_string host, i64 port);
void Amalgame_Pollen_Pollen_StartListener(i64 port);
code_string Amalgame_Pollen_Pollen_Publish(code_string host, i64 port, code_string topicUuid, i64 topicVersion, code_string dataJson);
i64 Amalgame_Pollen_Pollen_WorkflowVersion();
code_string Amalgame_Pollen_Pollen_WorkflowActiveRole();

Amalgame_Pollen_Pollen* Amalgame_Pollen_Pollen_new() {
    Amalgame_Pollen_Pollen* self = (Amalgame_Pollen_Pollen*) GC_MALLOC(sizeof(Amalgame_Pollen_Pollen));
    return self;
}

code_bool Amalgame_Pollen_Pollen_WorkflowLoad(code_string workflowPath, code_string nodeName, i64 actualPort) {
    #line 116 "facade.am"
    i64 ok = 0;
    #line 117 "facade.am"
    { /* inline-C */
         ok = Amalgame_Pollen_Pollen_WorkflowLoad_c(workflowPath, nodeName,
                                                                  (int64_t) actualPort); 
    }
    #line 119 "facade.am"
    return ok != 0;
}

void Amalgame_Pollen_Pollen_WorkflowSetSharedDir(code_string path) {
    #line 126 "facade.am"
    { /* inline-C */
         Amalgame_Pollen_Pollen_WorkflowSetSharedDir_c(path); 
    }
}

void Amalgame_Pollen_Pollen_WorkflowSetSelf(code_string role, code_string host, i64 port) {
    #line 131 "facade.am"
    { /* inline-C */
         Amalgame_Pollen_Pollen_WorkflowSetSelf_c(role, host,
                                                               (int64_t) port); 
    }
}

void Amalgame_Pollen_Pollen_StartListener(i64 port) {
    #line 139 "facade.am"
    { /* inline-C */
         Amalgame_Pollen_Pollen_StartListener_c((int64_t) port); 
    }
}

code_string Amalgame_Pollen_Pollen_Publish(code_string host, i64 port, code_string topicUuid, i64 topicVersion, code_string dataJson) {
    #line 149 "facade.am"
    code_string mid = "";
    #line 150 "facade.am"
    { /* inline-C */
         mid = Amalgame_Pollen_Pollen_Publish_c(host, (int64_t) port,
                                                              topicUuid, (int64_t) topicVersion,
                                                              dataJson); 
    }
    #line 153 "facade.am"
    return mid;
}

i64 Amalgame_Pollen_Pollen_WorkflowVersion() {
    #line 157 "facade.am"
    i64 v = 0;
    #line 158 "facade.am"
    { /* inline-C */
         v = Amalgame_Pollen_Pollen_WorkflowVersion_c(); 
    }
    #line 159 "facade.am"
    return v;
}

code_string Amalgame_Pollen_Pollen_WorkflowActiveRole() {
    #line 163 "facade.am"
    code_string r = "";
    #line 164 "facade.am"
    { /* inline-C */
         r = Amalgame_Pollen_Pollen_WorkflowActiveRole_c(); 
    }
    #line 165 "facade.am"
    return r;
}


/* Library — no entry point */
