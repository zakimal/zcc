#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct Type Type;
typedef struct Node Node;
typedef struct Member Member;

/*** strings.c ***/

char *format(char *fmt, ...);

/*** tokenize.c ***/

// Token
typedef enum
{
    TK_IDENT,   // Identifiers
    TK_PUNCT,   // Punctuators
    TK_KEYWORD, // Keywords
    TK_STR,     // String literals
    TK_NUM,     // Numeric literals
    TK_EOF,     // End-of-file markers
} TokenKind;

// Token type
typedef struct Token Token;
struct Token
{
    TokenKind kind; // Token kind
    Token *next;    // Next token
    int64_t val;    // If kind is TK_NUM, its value
    char *loc;      // Token location
    int len;        // Token length
    Type *ty;       // Used if TK_STR
    char *str;      // String literal contents including '\0'
    int line_no;    // Line number
};

void error(char *fmt, ...);
void error_at(char *loc, char *fmt, ...);
void error_tok(Token *tok, char *fmt, ...);
void warn_tok(Token *tok, char *fmt, ...);
bool equal(Token *tok, char *op);
Token *skip(Token *tok, char *op);
bool consume(Token **rest, Token *tok, char *str);
Token *tokenize_file(char *filename);

#define unreachable() \
    error("internal error at %s:%d", __FILE__, __LINE__)

/*** parse.c ***/

// Variable or function
typedef struct Var Var;
struct Var
{
    Var *next;
    char *name;    // Variable name
    Type *ty;      // Type
    bool is_local; // local or global/function

    // Local variable
    int offset;

    // Global variable or function
    bool is_function;
    bool is_definition;
    bool is_static;

    // Global variable
    char *init_data;

    // Function
    Var *params;
    Node *body;
    Var *locals;
    int stack_size;
};

// AST node
typedef enum
{
    ND_NULL_EXPR, // Do nothing
    ND_ADD,       // +
    ND_SUB,       // -
    ND_MUL,       // *
    ND_DIV,       // /
    ND_MOD,       // %
    ND_BITAND,    // &
    ND_BITOR,     // |
    ND_BITXOR,    // ^
    ND_SHL,       // <<
    ND_SHR,       // >>
    ND_NEG,       // unary -
    ND_EQ,        // ==
    ND_NE,        // !=
    ND_LT,        // <
    ND_LE,        // <=
    ND_ASSIGN,    // =
    ND_COND,      // ?:
    ND_COMMA,     // ,
    ND_MEMBER,    // . (struct member access)
    ND_ADDR,      // unary &
    ND_DEREF,     // unary *
    ND_NOT,       // !
    ND_BITNOT,    // ~
    ND_LOGAND,    // &&
    ND_LOGOR,     // ||
    ND_RETURN,    // "return"
    ND_IF,        // "if"
    ND_LOOP,      // "for" or "while"
    ND_SWITCH,    // "switch"
    ND_CASE,      // "case"
    ND_BLOCK,     // { ... }
    ND_GOTO,      // "goto"
    ND_LABEL,     // Labeled statement
    ND_FUNCALL,   // Function call
    ND_EXPR_STMT, // Expression statement
    ND_STMT_EXPR, // Statement expression
    ND_VAR,       // Variable
    ND_NUM,       // Integer
    ND_CAST,      // Type cast
} NodeKind;

// AST node type
typedef struct Node Node;
struct Node
{
    NodeKind kind;      // Node kind
    Node *next;         // Next node
    Type *ty;           // Type
    Token *tok;         // Representative token
    Node *lhs;          // Left-hand side
    Node *rhs;          // Right-hand side
    Node *cond;         // Condition
    Node *then;         // Then
    Node *els;          // Else
    Node *body;         // Block or statement expression
    Member *member;     // Struct member access
    char *funcname;     // Function call
    Type *func_ty;      // Function (argument) type
    Node *args;         // Arguments
    Node *init;         // Initialization
    Node *inc;          // Increment
    char *brk_label;    // Break
    char *cont_label;   // Continue
    char *label;        // Label in C code
    char *unique_label; // Label in assemble code
    Node *goto_next;    // Goto statement
    Node *case_next;    // Switch-cases
    Node *default_case; // Default
    Var *var;           // Variable, Used if kind == ND_VAR
    int64_t val;        // Numeric literal, Used if kind == ND_NUM
};

Node *new_cast(Node *expr, Type *ty);
Var *parse(Token *tok);

/*** type.c ***/

typedef enum
{
    TY_VOID,
    TY_BOOL,
    TY_CHAR,
    TY_SHORT,
    TY_INT,
    TY_LONG,
    TY_ENUM,
    TY_PTR,
    TY_FUNC,
    TY_ARRAY,
    TY_STRUCT,
    TY_UNION,
} TypeKind;

struct Type
{
    TypeKind kind;
    int size;
    int align;
    Type *base;
    Token *name;
    int array_len;
    Member *members;
    Type *return_ty;
    Type *params;
    Type *next;
};

// Struct member
struct Member
{
    Member *next;
    Type *ty;
    Token *tok; // for error message
    Token *name;
    int offset;
};

extern Type *ty_void;
extern Type *ty_bool;
extern Type *ty_char;
extern Type *ty_int;
extern Type *ty_long;
extern Type *ty_short;

bool is_integer(Type *ty);
Type *copy_type(Type *ty);
Type *pointer_to(Type *base);
Type *func_type(Type *return_ty);
Type *array_of(Type *base, int size);
Type *enum_type(void);
Type *struct_type(void);
void add_type(Node *node);

/*** codegen.c ***/

void codegen(Var *prog, FILE *out);
int align_to(int n, int align);