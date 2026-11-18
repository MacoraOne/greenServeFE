#ifndef GSFE_VALUE_H
#define GSFE_VALUE_H

#include <stddef.h>
#include <stdio.h>

/*
 * Forward declarations
 *
 * These types are defined below or in other runtime headers. Forward
 * declarations allow the value representation to refer to them without
 * requiring their full definitions here.
 */
typedef struct GSValue GSValue;
typedef struct GSArray GSArray;
typedef struct GSObject GSObject;
typedef struct GSEnvironment GSEnvironment;
typedef struct GSCode GSCode;
typedef struct GSIterator GSIterator;

/*
 * Native function signature
 *
 * A native function receives an array of GSValue arguments and the number
 * of arguments supplied. It returns its result as a GSValue.
 */
typedef GSValue (*GSNativeFn)(GSValue*, size_t);

/*
 * Runtime value categories
 *
 * GSValueType identifies which member of GSValue.as contains the active
 * representation. Keep this order and the enumerator names stable because
 * runtime code may depend on these tags.
 */
typedef enum {
    V_NULL,      /* No value. */
    V_NUMBER,    /* Numeric value stored as a double. */
    V_BOOL,      /* Boolean value stored as an int. */
    V_STRING,    /* Reference-counted character data. */
    V_ARRAY,     /* Reference-counted array. */
    V_OBJECT,    /* Reference-counted object with named properties. */
    V_FUNCTION,  /* Language function with code and captured environment. */
    V_NATIVE,    /* Native C function. */
    V_ITER       /* Iterator over an iterable value. */
} GSValueType;

/*
 * General runtime value
 *
 * The type field determines which member of the as union is valid.
 * The union keeps each value compact by storing only the representation
 * needed by the active type.
 */
struct GSValue {
    GSValueType type;

    union {
        double number;  /* V_NUMBER: numeric payload. */
        int boolean;    /* V_BOOL: boolean payload. */

        /* V_STRING: owned character buffer and its reference count. */
        struct {
            char* data;
            size_t refs;
        } string;

        GSArray* array;    /* V_ARRAY: pointer to array storage. */
        GSObject* object;  /* V_OBJECT: pointer to object storage. */

        /* V_FUNCTION: executable code and its captured lexical scope. */
        struct {
            GSCode* code;
            GSEnvironment* closure;
        } function;

        GSNativeFn native; /* V_NATIVE: C function pointer. */
        GSIterator* iter;  /* V_ITER: iterator state. */
    } as;
};

/*
 * Iterator state
 *
 * refs tracks shared ownership, index records the current iteration
 * position, and iterable retains the value being traversed.
 */
struct GSIterator {
    size_t refs;
    size_t index;
    GSValue iterable;
};

/*
 * Dynamic array storage
 *
 * refs tracks shared ownership. count is the number of initialized items,
 * capacity is the allocated item capacity, and items points to the storage.
 */
struct GSArray {
    size_t refs;
    size_t count;
    size_t capacity;
    GSValue* items;
};

/* One named property stored in a GSObject. */
typedef struct {
    char* key;
    GSValue value;
} GSProperty;

/*
 * Dynamic object storage
 *
 * The items array contains key/value properties. count tracks the number
 * of properties currently stored, while capacity tracks allocated slots.
 */
struct GSObject {
    size_t refs;
    size_t count;
    size_t capacity;
    GSProperty* items;
};

/*
 * Environment binding
 *
 * Each binding associates a variable name with a runtime value. The next
 * pointer links bindings together in the environment's binding list.
 */
typedef struct GSBinding {
    char* name;
    GSValue value;
    struct GSBinding* next;
} GSBinding;

/*
 * Lexical environment
 *
 * parent links to the enclosing scope. bindings is the head of this
 * environment's linked list of local variable bindings. refs tracks
 * shared ownership of the environment.
 */
struct GSEnvironment {
    size_t refs;
    GSEnvironment* parent;
    GSBinding* bindings;
};

/* -------------------------------------------------------------------------
 * Type information and value constructors
 * ------------------------------------------------------------------------- */

/* Return the readable name associated with a runtime value type. */
const char *gs_type_name(GSValueType);

/* Construct values for the built-in runtime types. */
GSValue gs_null(void);
GSValue gs_number(double);
GSValue gs_bool(int);
GSValue gs_string(const char*);
GSValue gs_array(void);
GSValue gs_object(void);
GSValue gs_native(GSNativeFn);
GSValue gs_function(GSCode*, GSEnvironment*);
GSValue gs_iterator(GSValue);

/* -------------------------------------------------------------------------
 * Value ownership, copying, and conversion
 * ------------------------------------------------------------------------- */

/* Retain or release the resources referenced by a value. */
void gs_retain(GSValue);
void gs_release(GSValue);

/* Create a value copy according to the runtime's copy semantics. */
GSValue gs_copy(GSValue);

/* Test truthiness and extract primitive representations. */
int gs_truthy(GSValue);
double gs_number_value(GSValue);
const char*gs_cstring(GSValue);

/* -------------------------------------------------------------------------
 * Array operations
 * ------------------------------------------------------------------------- */

/* Append an item, query the item count, or retrieve an item by position. */
void gs_array_push(GSValue*, GSValue);
size_t gs_array_count(GSValue);
GSValue gs_array_get(GSValue, size_t);

/* Replace an item at an index and remove/return the final item. */
int gs_array_set(GSValue*, size_t, GSValue);
GSValue gs_array_pop(GSValue*);

/* -------------------------------------------------------------------------
 * Object and indexed access operations
 * ------------------------------------------------------------------------- */

/* Query object property count and retrieve a key by its position. */
size_t gs_object_count(GSValue);
const char*gs_object_key(GSValue, size_t);

/* Read or write an indexed value using a runtime index value. */
GSValue gs_index(GSValue, GSValue);
int gs_set_index(GSValue, GSValue, GSValue);

/* Set or retrieve an object property by its string key. */
void gs_object_set(GSValue*, const char*, GSValue);
GSValue gs_object_get(GSValue, const char*);

/* -------------------------------------------------------------------------
 * Environment and variable-binding operations
 * ------------------------------------------------------------------------- */

/* Create an environment whose parent is the supplied enclosing scope. */
GSEnvironment*env_new(GSEnvironment*);

/* Manage the environment's shared ownership. */
void env_retain(GSEnvironment*);
void env_release(GSEnvironment*);

/* Define a local binding, update an existing binding, or look up a name. */
void env_define(GSEnvironment*, const char*, GSValue);
int env_set(GSEnvironment*, const char*, GSValue);
int env_get(GSEnvironment*, const char*, GSValue*);

/* -------------------------------------------------------------------------
 * Output, comparison, and operators
 * ------------------------------------------------------------------------- */

/* Write a human-readable representation of a value to the given stream. */
void gs_print(FILE*, GSValue);

/* Compare two runtime values and evaluate a binary operation. */
int gs_equal(GSValue, GSValue);
GSValue gs_binary(int, GSValue, GSValue);

#endif /* GSFE_VALUE_H */
