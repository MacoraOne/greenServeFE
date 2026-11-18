#ifndef GSFE_LEXER_H
#define GSFE_LEXER_H

/*
 * Public declarations for the greenServeFE source lexer.
 * This header exposes token types, token storage, and lexer functions.
 */

#include <stddef.h>

/*
 * Token categories recognized by the lexer.
 *
 * Keep the enumerators in their existing order: their implicit numeric
 * values may be used by the lexer, parser, or other runtime components.
 */
typedef enum {
    /* End-of-input and lexical-error markers. */
    TOK_EOF,
    TOK_ERROR,

    /* Literal and identifier values. */
    TOK_IDENTIFIER,
    TOK_NUMBER,
    TOK_STRING,

    /* Language keywords. */
    TOK_DEFINE,
    TOK_IF,
    TOK_ELSE,
    TOK_FOR,
    TOK_IN,
    TOK_WHILE,
    TOK_FUNC,
    TOK_RETURN,
    TOK_BREAK,
    TOK_CONTINUE,
    TOK_IMPORT,
    TOK_FROM,
    TOK_TRUE,
    TOK_FALSE,
    TOK_NULL,

    /* Grouping and punctuation tokens. */
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_COMMA,
    TOK_DOT,
    TOK_COLON,
    TOK_SEMICOLON,

    /* Arithmetic operators. */
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,

    /* Unary and assignment/equality operators. */
    TOK_BANG,
    TOK_EQUAL,
    TOK_EQUAL_EQUAL,
    TOK_BANG_EQUAL,

    /* Relational operators. */
    TOK_LESS,
    TOK_LESS_EQUAL,
    TOK_GREATER,
    TOK_GREATER_EQUAL,

    /* Logical operators. */
    TOK_AND_AND,
    TOK_OR_OR
} TokenType;

/*
 * One token produced by the lexer.
 *
 * type:
 *     The token category from TokenType.
 * lexeme:
 *     The token's source text (owned according to the lexer implementation).
 * number:
 *     Numeric value associated with number tokens.
 * line, column:
 *     Source position where the token begins.
 */
typedef struct {
    TokenType type;
    char *lexeme;
    double number;
    int line;
    int column;
} Token;

/*
 * Dynamic array containing the tokens generated for a source file.
 *
 * items:
 *     Allocated array of Token entries.
 * count:
 *     Number of populated entries.
 * capacity:
 *     Number of entries currently allocated in items.
 */
typedef struct {
    Token *items;
    size_t count;
    size_t capacity;
} TokenList;

/*
 * Lex source text and return the resulting token list.
 * The returned list must be released with free_tokens().
 */
TokenList lex_source(const char *source);

/*
 * Release token-list storage and any token-owned resources.
 */
void free_tokens(TokenList *tokens);

/*
 * Return the readable name associated with a token category.
 */
const char *token_type_name(TokenType type);

#endif /* GSFE_LEXER_H */
