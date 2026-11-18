#ifndef GSFE_VM_H
#define GSFE_VM_H

/*
 * greenServeFE Virtual Machine Interface
 *
 * This header exposes the public interface for creating, configuring,
 * executing, and destroying a greenServeFE virtual machine.
 *
 * The VM executes compiled bytecode represented by GSCode. The bytecode
 * definitions are provided by bytecode.h.
 */

#include "bytecode.h"

/*
 * Forward declaration of the virtual machine structure.
 *
 * The GSVM implementation is intentionally opaque to users of this header.
 * Callers work with GSVM pointers and use the functions below rather than
 * accessing the VM's internal fields directly.
 */
typedef struct GSVM GSVM;

/*
 * Allocate and initialize a new virtual machine.
 *
 * Returns:
 *   A pointer to the newly created GSVM, or NULL if creation fails.
 */
GSVM *vm_new(void);

/*
 * Release a virtual machine and its owned resources.
 *
 * The VM pointer must refer to a VM created by vm_new().
 */
void vm_free(GSVM *);

/*
 * Execute a compiled greenServeFE code object.
 *
 * Parameters:
 *   vm   - The virtual machine that will execute the code.
 *   code - The compiled bytecode to run.
 *
 * Returns:
 *   The execution status reported by the VM.
 */
int vm_run(GSVM *, GSCode *);

/*
 * Enable or disable VM execution tracing.
 *
 * The meaning of the integer trace flag is defined by the VM
 * implementation (typically zero disables tracing and nonzero enables it).
 */
void vm_set_trace(GSVM *, int);

/*
 * Associate source-file information with the virtual machine.
 *
 * The source path and source text/name information are used by the runtime
 * for diagnostics and source-aware error reporting.
 */
void vm_set_source(GSVM *, const char *, const char *);

/*
 * Retrieve the number of runtime errors recorded by the VM.
 *
 * The const-qualified VM pointer indicates that this query does not modify
 * the virtual machine.
 */
int vm_runtime_errors(const GSVM *);

#endif /* GSFE_VM_H */
