#ifndef GSFE_PARSER_H
#define GSFE_PARSER_H

#include "lexer.h"
#include <stddef.h>

/*
 * Expression node kinds.
 *
 * Each Expr stores one of these values in its "type" field. The matching
 * payload is stored in the corresponding member of the "as" union below.
 */
typedef enum {
    EX_NUMBER,      /* Numeric literal. */
    EX_STRING,      /* String literal. */
    EX_BOOL,        /* Boolean literal. */
    EX_NULL,        /* Null literal. */
    EX_IDENTIFIER,  /* Variable or identifier reference. */
    EX_ARRAY,       /* Array literal containing expression elements. */
    EX_OBJECT,      /* Object literal containing key/value entries. */
    EX_UNARY,       /* Unary operation, such as logical or numeric negation. */
    EX_BINARY,      /* Binary operation with a left and right operand. */
    EX_ASSIGN,      /* Assignment of a value to a target expression. */
    EX_CALL,        /* Function or callable invocation. */
    EX_INDEX,       /* Indexed access to an object or collection. */
    EX_MEMBER,      /* Named member access on an object. */
    EX_FUNCTION     /* Function expression with parameters and a body. */
} ExprType;

/*
 * Statement node kinds.
 *
 * These values identify the statement payload stored in the Stmt "as" union.
 */
typedef enum {
    ST_BLOCK,       /* A sequence of statements grouped into a block. */
    ST_EXPR,        /* A standalone expression statement. */
    ST_DEFINE,      /* A variable definition. */
    ST_IF,          /* Conditional statement with then/else branches. */
    ST_WHILE,       /* While loop. */
    ST_FOR,         /* For loop over an iterable expression. */
    ST_FUNC,        /* Named function declaration. */
    ST_RETURN,      /* Function return statement. */
    ST_BREAK,       /* Loop break statement. */
    ST_CONTINUE,    /* Loop continue statement. */
    ST_IMPORT,      /* Import of a module. */
    ST_FROM_IMPORT  /* Import of selected names from a module. */
} StmtType;

/* Forward declarations allow expressions and statements to refer to each other. */
typedef struct Expr Expr;
typedef struct Stmt Stmt;

/* One key/value pair in an object literal. */
typedef struct {
    char *key;      /* Object property name. */
    Expr *value;    /* Expression that produces the property's value. */
} ObjectEntry;

/* A dynamically sized list of imported or otherwise collected names. */
typedef struct {
    char **names;   /* Array of name strings. */
    size_t count;   /* Number of names in the array. */
} NameList;

/*
 * Expression AST node.
 *
 * "type" selects the active member of "as". The source location is retained
 * so later compiler/runtime stages can report useful diagnostics.
 */
struct Expr {
    ExprType type;  /* Kind of expression represented by this node. */
    int line;       /* Source line where the expression begins. */
    int column;     /* Source column where the expression begins. */

    /* Type-specific data for the expression node. */
    union {
        double number;      /* EX_NUMBER: numeric value. */
        char *string;       /* EX_STRING: string contents. */
        int boolean;        /* EX_BOOL: boolean value. */
        char *identifier;   /* EX_IDENTIFIER: identifier name. */

        /* EX_ARRAY: ordered collection of element expressions. */
        struct {
            Expr **items;   /* Array of pointers to element expressions. */
            size_t count;   /* Number of elements. */
        } array;

        /* EX_OBJECT: ordered collection of object properties. */
        struct {
            ObjectEntry *items; /* Array of key/value entries. */
            size_t count;       /* Number of entries. */
        } object;

        /* EX_UNARY: operator applied to a single expression. */
        struct {
            TokenType op;   /* Unary operator token. */
            Expr *value;    /* Operand expression. */
        } unary;

        /* EX_BINARY: operator joining two expressions. */
        struct {
            TokenType op;   /* Binary operator token. */
            Expr *left;     /* Left operand. */
            Expr *right;    /* Right operand. */
        } binary;

        /* EX_ASSIGN: target expression and assigned value. */
        struct {
            Expr *target;   /* Assignment destination. */
            Expr *value;    /* Expression assigned to the target. */
        } assign;

        /* EX_CALL: callable expression and its argument list. */
        struct {
            Expr *callee;   /* Function or callable being invoked. */
            Expr **args;    /* Array of argument expressions. */
            size_t count;   /* Number of arguments. */
        } call;

        /* EX_INDEX: collection/object expression and index expression. */
        struct {
            Expr *object;   /* Expression whose element is accessed. */
            Expr *index;    /* Expression that determines the index/key. */
        } index;

        /* EX_MEMBER: object expression and the requested member name. */
        struct {
            Expr *object;   /* Expression that owns the member. */
            char *name;     /* Member/property name. */
        } member;

        /* EX_FUNCTION: parameters and body of a function expression. */
        struct {
            char **params;      /* Array of parameter names. */
            size_t param_count; /* Number of parameters. */
            Stmt *body;         /* Function body statement. */
        } function;
    } as;
};

/*
 * Statement AST node.
 *
 * Statements are linked through "next", while nested statements are stored
 * in the relevant payload. As with Expr, "type" selects the active union member.
 */
struct Stmt {
    StmtType type;  /* Kind of statement represented by this node. */
    int line;       /* Source line where the statement begins. */
    int column;     /* Source column where the statement begins. */
    Stmt *next;     /* Next statement in the same statement sequence. */

    /* Type-specific data for the statement node. */
    union {
        /* ST_BLOCK: first statement in the block's linked statement sequence. */
        struct {
            Stmt *first;
        } block;

        /* ST_EXPR: expression evaluated as a standalone statement. */
        struct {
            Expr *expr;
        } expr;

        /* ST_DEFINE: variable name and initializer expression. */
        struct {
            char *name;
            Expr *value;
        } define;

        /* ST_IF: condition and the two possible branches. */
        struct {
            Expr *condition;
            Stmt *then_branch;
            Stmt *else_branch;
        } if_stmt;

        /* ST_WHILE: loop condition and body. */
        struct {
            Expr *condition;
            Stmt *body;
        } while_stmt;

        /* ST_FOR: loop variable, iterable expression, and body. */
        struct {
            char *name;
            Expr *iterable;
            Stmt *body;
        } for_stmt;

        /* ST_FUNC: declared function name, parameters, and body. */
        struct {
            char *name;
            char **params;
            size_t param_count;
            Stmt *body;
        } func;

        /* ST_RETURN: optional/value expression returned by the statement. */
        struct {
            Expr *value;
        } return_stmt;

        /* ST_IMPORT: module name to import. */
        struct {
            char *module;
        } import_stmt;

        /* ST_FROM_IMPORT: module name and selected imported names. */
        struct {
            char *module;
            NameList names;
        } from_import;
    } as;
};

/*
 * Complete parser output.
 *
 * "program" points to the parsed statement sequence. "errors" records the
 * number of parse errors encountered while building the program.
 */
typedef struct {
    Stmt *program;
    int errors;
} Program;

/* Parse a token list and return the resulting program AST and error count. */
Program parse_program(TokenList *);

/* Release the AST and resources owned by a parsed program. */
void free_program(Program *);

/* Print the parse errors recorded in a program result. */
void print_parse_errors(const Program *);

#endif
