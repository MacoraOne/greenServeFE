/*
 * bytecode.c
 *
 * Bytecode construction, reference ownership, instruction patching,
 * opcode naming, and recursive bytecode disassembly.
 *
 * This pass documents and lays out the existing implementation for
 * maintainers. It intentionally preserves the original operations.
 */

#include "bytecode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Duplicate a string.
 * A NULL input becomes an empty string. The returned allocation belongs
 * to the caller and must eventually be released with free().
 */
static char *sd(const char *s) {
    return strdup(s ? s : "");
}

/*
 * Expand a dynamically allocated array.
 * Capacity begins at eight elements and doubles on each expansion. The
 * updated capacity is written through n. Allocation failure terminates
 * the process with the existing exit status.
 */
static void *grow(void *p, size_t *n, size_t es) {
    size_t c = *n ? *n * 2 : 8;
    void *q = realloc(p, c * es);

    if (!q)
        exit(2);

    *n = c;
    return q;
}

/*
 * Allocate and initialize a new bytecode object.
 * calloc clears the structure, refs establishes its initial ownership
 * count, and sd stores an independently allocated copy of the code name.
 */
GSCode *code_new(const char *n) {
    GSCode *c = calloc(1, sizeof *c);

    if (!c)
        exit(2);

    c->refs = 1;
    c->name = sd(n);
    return c;
}

/*
 * Retain a bytecode object.
 * A non-NULL object receives one additional reference. NULL remains a
 * harmless no-op, allowing optional child pointers to be passed safely.
 */
void code_retain(GSCode *c) {
    if (c)
        c->refs++;
}

/*
 * Release a reference to a bytecode object.
 * The object is destroyed only when this release reduces its reference
 * count to zero. Its owned constants, child objects, name strings, and
 * backing arrays are then released in turn.
 */
void code_release(GSCode *c) {
    if (!c || --c->refs)
        return;

    /* Release every value held by the constants table. */
    for (size_t i = 0; i < c->const_count; i++)
        gs_release(c->constants[i]);

    /* Release the references held for nested child code objects. */
    for (size_t i = 0; i < c->child_count; i++)
        code_release(c->children[i]);

    /* Each name is a separately allocated string. */
    for (size_t i = 0; i < c->name_count; i++)
        free(c->names[i]);

    /* Release the name and all dynamically allocated backing arrays. */
    free(c->name);
    free(c->code);
    free(c->constants);
    free(c->names);
    free(c->children);

    /* The structure itself is the final allocation to release. */
    free(c);
}

/*
 * Append an instruction to the bytecode stream.
 * Ensure that the instruction array has room, then store the opcode,
 * argument, and source line at the current end. Return the inserted index.
 */
int code_emit(GSCode *c, GSOp op, int arg, int line) {
    if (c->count == c->capacity)
        c->code = grow(c->code, &c->capacity, sizeof *c->code);

    c->code[c->count] = (GSInstr){(uint8_t)op, arg, line};
    return (int)c->count++;
}

/*
 * Add a value to the constants table.
 * Expand the table when it is full. gs_copy gives this code object's
 * owned copy of the value. The returned integer identifies its table slot.
 */
int code_add_const(GSCode *c, GSValue v) {
    if (c->const_count == c->const_capacity)
        c->constants = grow(c->constants, &c->const_capacity,
                            sizeof *c->constants);

    c->constants[c->const_count] = gs_copy(v);
    return (int)c->const_count++;
}

/*
 * Find a name in the name table, or append it if it is not present.
 * Existing names reuse their original index. New names are duplicated so
 * the table owns its stored string independently of the caller's buffer.
 */
int code_add_name(GSCode *c, const char *n) {
    /* Search existing entries first to avoid duplicate table entries. */
    for (size_t i = 0; i < c->name_count; i++)
        if (!strcmp(c->names[i], n))
            return (int)i;

    /* Grow the pointer array if no free slot remains. */
    if (c->name_count == c->name_capacity)
        c->names = grow(c->names, &c->name_capacity, sizeof *c->names);

    /* Store a duplicated name and return the index assigned to it. */
    c->names[c->name_count] = sd(n);
    return (int)c->name_count++;
}

/*
 * Add a child code object to its parent's child table.
 * The child is retained before its pointer is stored. The returned index
 * is the position at which the child was appended.
 */
int code_add_child(GSCode *c, GSCode *x) {
    if (c->child_count == c->child_capacity)
        c->children = grow(c->children, &c->child_capacity,
                           sizeof *c->children);

    code_retain(x);
    c->children[c->child_count] = x;
    return (int)c->child_count++;
}

/*
 * Patch the argument of an already-emitted instruction.
 * The bounds check prevents writes for negative or out-of-range positions.
 * This is useful when a jump target is not known at initial emission time.
 */
void code_patch(GSCode *c, int at, int arg) {
    if (at >= 0 && (size_t)at < c->count)
        c->code[at].arg = arg;
}

/*
 * Convert an opcode value to its printable mnemonic.
 * The mnemonic array follows the GSOp enumeration order. Unknown numeric
 * values use the "UNKNOWN" fallback rather than indexing beyond the array.
 */
const char *op_name(GSOp o) {
    static const char *n[] = {
        "NOP", "LOAD_CONST", "LOAD_NAME", "STORE_NAME", "POP_TOP",
        "ADD", "SUB", "MUL", "DIV", "MOD", "NEG", "NOT",
        "EQ", "NE", "LT", "LE", "GT", "GE", "AND", "OR",
        "JUMP", "JUMP_IF_FALSE", "JUMP_IF_TRUE", "CALL", "RETURN",
        "BUILD_ARRAY", "BUILD_OBJECT", "GET_INDEX", "SET_INDEX",
        "GET_MEMBER", "SET_MEMBER", "MAKE_FUNCTION", "IMPORT",
        "FROM_IMPORT", "ITER_INIT", "ITER_NEXT", "HALT"
    };

    return o < sizeof(n) / sizeof(n[0]) ? n[o] : "UNKNOWN";
}

/*
 * Print a bytecode object and recursively print all of its child objects.
 * prefix is included at the start of each output line. Each recursive
 * call extends that prefix to visually indent the nested code structure.
 */
void code_disassemble(const GSCode *c, const char *prefix) {
    /* A missing code object has nothing to display. */
    if (!c)
        return;

    /* Display the code object's name and declared parameter count. */
    printf("%sCode %s (params=%d)\n",
           prefix ? prefix : "", c->name, c->param_count);

    /* Display each instruction with its index, mnemonic, and argument. */
    for (size_t i = 0; i < c->count; i++) {
        GSInstr in = c->code[i];

        printf("%s%04zu %-18s %d\n",
               prefix ? prefix : "", i, op_name((GSOp)in.op), in.arg);
    }

    /* Visit child code objects recursively, increasing indentation. */
    for (size_t i = 0; i < c->child_count; i++) {
        printf("%schild %zu:\n", prefix ? prefix : "", i);
        char b[128];

        snprintf(b, sizeof b, "%s  ", prefix ? prefix : "");
        code_disassemble(c->children[i], b);
    }
}
