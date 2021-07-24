#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Type Type;
typedef struct Node Node;

/*** tokenize.c ***/

// Token
typedef enum
{
    TK_IDENT,   // Identifiers
    TK_PUNCT,   // Punctuators
    TK_KEYWORD, // Keywords
    TK_NUM,     // Numeric literals
    TK_EOF,     // End-of-file markers
} TokenKind;

// Token type
typedef struct Token Token;
struct Token
{
    TokenKind kind; // Token kind
    Token *next;    // Next token
    int val;        // If kind is TK_NUM, its value
    char *loc;      // Token location
    int len;        // Token length
};

void error(char *fmt, ...);
void error_at(char *loc, char *fmt, ...);
void error_tok(Token *tok, char *fmt, ...);
bool equal(Token *tok, char *op);
Token *skip(Token *tok, char *op);
bool consume(Token **rest, Token *tok, char *str);
Token *tokenize(char *input);

/*** parse.c ***/

// Variable or function
typedef struct Obj Obj;
struct Obj
{
    Obj *next;
    char *name;    // Variable name
    Type *ty;      // Type
    bool is_local; // local or global/function

    // Local variable
    int offset;

    // Global variable or function
    bool is_function;

    // Function
    Obj *params;
    Node *body;
    Obj *locals;
    int stack_size;
};

// AST node
typedef enum
{
    ND_ADD,       // +
    ND_SUB,       // -
    ND_MUL,       // *
    ND_DIV,       // /
    ND_NEG,       // unary -
    ND_EQ,        // ==
    ND_NE,        // !=
    ND_LT,        // <
    ND_LE,        // <=
    ND_ASSIGN,    // =
    ND_ADDR,      // unary &
    ND_DEREF,     // unary *
    ND_RETURN,    // "return"
    ND_IF,        // "if"
    ND_LOOP,      // "for" or "while"
    ND_BLOCK,     // { ... }
    ND_FUNCALL,   // Function call
    ND_EXPR_STMT, // Expression statement
    ND_VAR,       // Variable
    ND_NUM,       // Integer
} NodeKind;

// AST node type
typedef struct Node Node;
struct Node
{
    NodeKind kind;  // Node kind
    Node *next;     // Next node
    Type *ty;       // Type
    Token *tok;     // Representative token
    Node *lhs;      // Left-hand side
    Node *rhs;      // Right-hand side
    Node *cond;     // Condition
    Node *then;     // Then
    Node *els;      // Else
    Node *body;     // Block
    char *funcname; // Function call
    Node *args;     // Arguments
    Node *init;     // Initialization
    Node *inc;      // Increment
    Obj *var;       // Used if kind == ND_VAR
    int val;        // Used if kind == ND_NUM
};

Obj *parse(Token *tok);

/*** type.c ***/

typedef enum
{
    TY_INT,
    TY_PTR,
    TY_FUNC,
    TY_ARRAY,
} TypeKind;

struct Type
{
    TypeKind kind;
    int size;
    Type *base;
    Token *name;
    int array_len;
    Type *return_ty;
    Type *params;
    Type *next;
};

extern Type *ty_int;

bool is_integer(Type *ty);
Type *copy_type(Type *ty);
Type *pointer_to(Type *base);
Type *func_type(Type *return_ty);
Type *array_of(Type *base, int size);
void add_type(Node *node);

/*** codegen.c ***/

void codegen(Obj *prog);