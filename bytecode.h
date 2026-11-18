#ifndef GSFE_BYTECODE_H
#define GSFE_BYTECODE_H

/* Standard types used by bytecode storage and instruction arguments. */
#include <stddef.h>
#include <stdint.h>

/* GSValue is used for constants stored in compiled code objects. */
#include "value.h"

/*
 * Bytecode operation identifiers.
 *
 * The order is significant: unless an explicit value is assigned, each
 * operation receives the next integer value from the C compiler.
 */
typedef enum {
    /* No-operation and constant/name stack operations. */
    OP_NOP,
    OP_LOAD_CONST,
    OP_LOAD_NAME,
    OP_STORE_NAME,
    OP_POP_TOP,

    /* Unary, arithmetic, comparison, and logical operations. */
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_NEG,
    OP_NOT,
    OP_EQ,
    OP_NE,
    OP_LT,
    OP_LE,
    OP_GT,
    OP_GE,
    OP_AND,
    OP_OR,

    /* Control-flow operations. */
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_JUMP_IF_TRUE,

    /* Function invocation and return. */
    OP_CALL,
    OP_RETURN,

    /* Array, object, index, and member operations. */
    OP_BUILD_ARRAY,
    OP_BUILD_OBJECT,
    OP_GET_INDEX,
    OP_SET_INDEX,
    OP_GET_MEMBER,
    OP_SET_MEMBER,

    /* Function creation and module import operations. */
    OP_MAKE_FUNCTION,
    OP_IMPORT,
    OP_FROM_IMPORT,

    /* Iterator operations and the final execution stop. */
    OP_ITER_INIT,
    OP_ITER_NEXT,
    OP_HALT
} GSOp;

/*
 * One bytecode instruction.
 *
 * op   identifies the GSOp operation.
 * arg  carries the operation-specific operand.
 * line records the source-code line associated with the instruction.
 */
typedef struct {
    uint8_t op;
    int32_t arg;
    int line;
} GSInstr;

/*
 * Compiled code object.
 *
 * The object owns its instruction sequence, constant table, name table,
 * and child-code references. The capacity fields track allocated storage;
 * the count fields track the number of entries currently in use.
 */
struct GSCode {
    /* Reference count and human-readable code-object name. */
    size_t refs;
    char *name;

    /* Dynamically allocated bytecode instruction sequence. */
    GSInstr *code;
    size_t count;
    size_t capacity;

    /* Constants referenced by LOAD_CONST instructions. */
    GSValue *constants;
    size_t const_count;
    size_t const_capacity;

    /* Names referenced by name-related instructions. */
    char **names;
    size_t name_count;
    size_t name_capacity;

    /* Nested or child code objects, such as compiled function bodies. */
    GSCode **children;
    size_t child_count;
    size_t child_capacity;

    /* Number of parameters expected by this code object. */
    int param_count;
};

/* Allocate and initialize a new code object with the supplied name. */
GSCode *code_new(const char *);

/* Increase or decrease a code object's reference count. */
void code_retain(GSCode *);
void code_release(GSCode *);

/* Append an instruction and return its instruction position/status. */
int code_emit(GSCode *, GSOp, int, int);

/* Add a constant, name, or child code object to the corresponding table. */
int code_add_const(GSCode *, GSValue);
int code_add_name(GSCode *, const char *);
int code_add_child(GSCode *, GSCode *);

/* Replace the operand of an instruction after its target is known. */
void code_patch(GSCode *, int, int);

/* Convert an operation identifier to its printable name. */
const char *op_name(GSOp);

/* Print a readable disassembly of a code object. */
void code_disassemble(const GSCode *, const char *);

#endif /* GSFE_BYTECODE_H */
