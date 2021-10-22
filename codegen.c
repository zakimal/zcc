#include "zcc.h"

static FILE *output_file;
static int depth;
static char *argreg8[] = {"%dil", "%sil", "%dl", "%cl", "%r8b", "%r9b"};
static char *argreg16[] = {"%di", "%si", "%dx", "%cx", "%r8w", "%r9w"};
static char *argreg32[] = {"%edi", "%esi", "%edx", "%ecx", "%r8d", "%r9d"};
static char *argreg64[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
static Var *current_fn;

static void gen_expr(Node *node);
static void gen_stmt(Node *node);

static void println(char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(output_file, fmt, ap);
    va_end(ap);
    fprintf(output_file, "\n");
}

static int count(void)
{
    static int i = 1;
    return i++;
}

static void push(void)
{
    println("  push %%rax");
    depth++;
}

static void pop(char *arg)
{
    println("  pop %s", arg);
    depth--;
}

// Round up `n` to the nearest multiple of `align`
// eg. align_to(5, 8) returns 8
// eg. align_to(11, 8) returns 16
int align_to(int n, int align)
{
    return (n + align - 1) / align * align;
}

// Compute the absolute address of a given node.
// It's an error if a given node does not reside in memory.
static void gen_addr(Node *node)
{
    switch (node->kind)
    {
    case ND_VAR:
        if (node->var->is_local)
        {
            // Local variable
            println("  lea %d(%%rbp), %%rax", node->var->offset);
        }
        else
        {
            // Global variable
            println("  lea %s(%%rip), %%rax", node->var->name);
        }
        return;
    case ND_DEREF:
        gen_expr(node->lhs);
        return;
    case ND_COMMA:
        gen_expr(node->lhs);
        gen_addr(node->rhs);
        return;
    case ND_MEMBER:
        gen_addr(node->lhs);
        println("  add $%d, %%rax", node->member->offset);
        return;
    }

    error_tok(node->tok, "not an lvalue");
}

// Load a value from where %rax is pointing to.
static void load(Type *ty)
{
    if (ty->kind == TY_ARRAY || ty->kind == TY_STRUCT || ty->kind == TY_UNION)
    {
        return;
    }

    char *insn = ty->is_unsigned ? "movz" : "movs";

    if (ty->size == 1)
    {
        println("  %sbl (%%rax), %%eax", insn);
    }
    else if (ty->size == 2)
    {
        println("  %swl (%%rax), %%eax", insn);
    }
    else if (ty->size == 4)
    {
        println("  movsxd (%%rax), %%rax");
    }
    else
    {
        println("  mov (%%rax), %%rax");
    }
}

// Store %rax to an address that the stack top is pointing to.
static void store(Type *ty)
{
    pop("%rdi");

    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION)
    {
        for (int i = 0; i < ty->size; i++)
        {
            println("  mov %d(%%rax), %%r8b", i);
            println("  mov %%r8b, %d(%%rdi)", i);
        }
        return;
    }

    if (ty->size == 1)
    {
        println("  mov %%al, (%%rdi)");
    }
    else if (ty->size == 2)
    {
        println("  mov %%ax, (%%rdi)");
    }
    else if (ty->size == 4)
    {
        println("  mov %%eax, (%%rdi)");
    }
    else
    {
        println("  mov %%rax, (%%rdi)");
    }
}

static void cmp_zero(Type *ty)
{
    if (is_integer(ty) && ty->size <= 4)
    {
        println("  cmp $0, %%eax");
    }
    else
    {
        println("  cmp $0, %%rax");
    }
}

enum
{
    I8,
    I16,
    I32,
    I64,
    U8,
    U16,
    U32,
    U64
};

static int getTypeId(Type *ty)
{
    switch (ty->kind)
    {
    case TY_CHAR:
        return ty->is_unsigned ? U8 : I8;
    case TY_SHORT:
        return ty->is_unsigned ? U16 : I16;
    case TY_INT:
        return ty->is_unsigned ? U32 : I32;
    case TY_LONG:
        return ty->is_unsigned ? U64 : I64;
    }
    return U64;
}

// The table for type casts
static char i32i8[] = "movsbl %al, %eax";
static char i32u8[] = "movzbl %al, %eax";
static char i32i16[] = "movswl %ax, %eax";
static char i32u16[] = "movzwl %ax, %eax";
static char i32i64[] = "movsxd %eax, %rax";
static char u32i64[] = "mov %eax, %eax";

static char *cast_table[][10] = {
    /* i8  */ {NULL, NULL, NULL, i32i64, i32u8, i32u16, NULL, i32i64},
    /* i16 */ {i32i8, NULL, NULL, i32i64, i32u8, i32u16, NULL, i32i64},
    /* i32 */ {i32i8, i32i16, NULL, i32i64, i32u8, i32u16, NULL, i32i64},
    /* i64 */ {i32i8, i32i16, NULL, NULL, i32u8, i32u16, NULL, NULL},
    /* u8  */ {i32i8, NULL, NULL, i32i64, NULL, NULL, NULL, i32i64},
    /* u16 */ {i32i8, i32i16, NULL, i32i64, i32u8, NULL, NULL, i32i64},
    /* u32 */ {i32i8, i32i16, NULL, u32i64, i32u8, i32u16, NULL, u32i64},
    /* u64 */ {i32i8, i32i16, NULL, NULL, i32u8, i32u16, NULL, NULL},
};

static void cast(Type *from, Type *to)
{
    if (to->kind == TY_VOID)
    {
        return;
    }

    if (to->kind == TY_BOOL)
    {
        cmp_zero(from);
        println("  setne %%al");
        println("  movzx %%al, %%eax");
        return;
    }

    int t1 = getTypeId(from);
    int t2 = getTypeId(to);
    if (cast_table[t1][t2])
    {
        println("  %s", cast_table[t1][t2]);
    }
}

// Generate code for a given node.
static void gen_expr(Node *node)
{
    println("  .loc 1 %d", node->tok->line_no);
    switch (node->kind)
    {
    case ND_NULL_EXPR:
        return;
    case ND_NUM:
        println("  mov $%ld, %%rax", node->val);
        return;
    case ND_NEG:
        gen_expr(node->lhs);
        println("  neg %%rax");
        return;
    case ND_VAR:
    case ND_MEMBER:
        gen_addr(node);
        load(node->ty);
        return;
    case ND_DEREF:
        gen_expr(node->lhs);
        load(node->ty);
        return;
    case ND_ADDR:
        gen_addr(node->lhs);
        return;
    case ND_ASSIGN:
        gen_addr(node->lhs);
        push();
        gen_expr(node->rhs);
        store(node->ty);
        return;
    case ND_STMT_EXPR:
        for (Node *n = node->body; n; n = n->next)
        {
            gen_stmt(n);
        }
        return;
    case ND_COMMA:
        gen_expr(node->lhs);
        gen_expr(node->rhs);
        return;
    case ND_CAST:
        gen_expr(node->lhs);
        cast(node->lhs->ty, node->ty);
        return;
    case ND_MEMZERO:
        // `rep stosb` is equivalent to `memset(%rdi, %al, %rcx)`.
        println("  mov $%d, %%rcx", node->var->ty->size);
        println("  lea %d(%%rbp), %%rdi", node->var->offset);
        println("  mov $0, %%al");
        println("  rep stosb");
        return;
    case ND_COND:
    {
        int c = count();
        gen_expr(node->cond);
        println("  cmp $0, %%rax");
        println("  je .L.else.%d", c);
        gen_expr(node->then);
        println("  jmp .L.end.%d", c);
        println(".L.else.%d:", c);
        gen_expr(node->els);
        println(".L.end.%d:", c);
        return;
    }
    case ND_NOT:
        gen_expr(node->lhs);
        println("  cmp $0, %%rax");
        println("  sete %%al");
        println("  movzx %%al, %%rax");
        return;
    case ND_BITNOT:
        gen_expr(node->lhs);
        println("  not %%rax");
        return;
    case ND_LOGAND:
    {
        int c = count();
        gen_expr(node->lhs);
        println("  cmp $0, %%rax");
        println("  je .L.false.%d", c);
        gen_expr(node->rhs);
        println("  cmp $0, %%rax");
        println("  je .L.false.%d", c);
        println("  mov $1, %%rax");
        println("  jmp .L.end.%d", c);
        println(".L.false.%d:", c);
        println("  mov $0, %%rax");
        println(".L.end.%d:", c);
        return;
    }
    case ND_LOGOR:
    {
        int c = count();
        gen_expr(node->lhs);
        println("  cmp $0, %%rax");
        println("  jne .L.true.%d", c);
        gen_expr(node->rhs);
        println("  cmp $0, %%rax");
        println("  jne .L.true.%d", c);
        println("  mov $0, %%rax");
        println("  jmp .L.end.%d", c);
        println(".L.true.%d:", c);
        println("  mov $1, %%rax");
        println(".L.end.%d:", c);
        return;
    }
    case ND_FUNCALL:
    {
        int nargs = 0;
        for (Node *arg = node->args; arg; arg = arg->next)
        {
            gen_expr(arg);
            push();
            nargs++;
        }

        for (int i = nargs - 1; i >= 0; i--)
        {
            pop(argreg64[i]);
        }

        println("  mov $0, %%rax");
        if (depth % 2 == 0)
        {
            println("  call %s", node->funcname);
        }
        else
        {
            println("  sub $8, %%rsp");
            println("  call %s", node->funcname);
            println("  add $8, %%rsp");
        }

        switch (node->ty->kind)
        {
        case TY_BOOL:
            println("  movzx %%al, %%eax");
            return;
        case TY_CHAR:
            if (node->ty->is_unsigned)
            {
                println("  movzbl %%al, %%eax");
            }
            else
            {
                println("  movsbl %%al, %%eax");
            }
            return;
        case TY_SHORT:
            if (node->ty->is_unsigned)
            {
                println("  movzwl %%ax, %%eax");
            }
            else
            {
                println("  movswl %%ax, %%eax");
            }
            return;
        }

        return;
    }
    }

    gen_expr(node->rhs);
    push();
    gen_expr(node->lhs);
    pop("%rdi");

    char *ax, *di, *dx;

    if (node->lhs->ty->kind == TY_LONG || node->lhs->ty->base)
    {
        ax = "%rax";
        di = "%rdi";
        dx = "%rdx";
    }
    else
    {
        ax = "%eax";
        di = "%edi";
        dx = "%edx";
    }

    switch (node->kind)
    {
    case ND_ADD:
        println("  add %s, %s", di, ax);
        return;
    case ND_SUB:
        println("  sub %s, %s", di, ax);
        return;
    case ND_MUL:
        println("  imul %s, %s", di, ax);
        return;
    case ND_DIV:
    case ND_MOD:
        if (node->ty->is_unsigned)
        {
            println("  mov $0, %s", dx);
            println("  div %s", di);
        }
        else
        {
            if (node->lhs->ty->size == 8)
            {
                println("  cqo");
            }
            else
            {
                println("  cdq");
            }
            println("  idiv %s", di);
        }

        if (node->kind == ND_MOD)
        {
            println("  mov %%rdx, %%rax");
        }
        return;
    case ND_BITAND:
        println("  and %%rdi, %%rax");
        return;
    case ND_BITOR:
        println("  or %%rdi, %%rax");
        return;
    case ND_BITXOR:
        println("  xor %%rdi, %%rax");
        return;
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LE:
        println("  cmp %s, %s", di, ax);
        if (node->kind == ND_EQ)
        {
            println("  sete %%al");
        }
        else if (node->kind == ND_NE)
        {
            println("  setne %%al");
        }
        else if (node->kind == ND_LT)
        {
            if (node->lhs->ty->is_unsigned)
            {
                println("  setb %%al");
            }
            else
            {
                println("  setl %%al");
            }
        }
        else if (node->kind == ND_LE)
        {
            if (node->lhs->ty->is_unsigned)
            {
                println("  setbe %%al");
            }
            else
            {
                println("  setle %%al");
            }
        }

        println("  movzb %%al, %%rax");
        return;
    case ND_SHL:
        println("  mov %%rdi, %%rcx");
        println("  shl %%cl, %s", ax);
        return;
    case ND_SHR:
        println("  mov %%rdi, %%rcx");
        if (node->lhs->ty->is_unsigned)
        {
            println("  shr %%cl, %s", ax);
        }
        else
        {
            println("  sar %%cl, %s", ax);
        }
        return;
    }

    error_tok(node->tok, "invalid expression");
}

static void gen_stmt(Node *node)
{
    println("  .loc 1 %d", node->tok->line_no);
    switch (node->kind)
    {
    case ND_IF:
    {
        int c = count();
        gen_expr(node->cond);
        println("  cmp $0, %%rax");
        println("  je  .L.else.%d", c);
        gen_stmt(node->then);
        println("  jmp .L.end.%d", c);
        println(".L.else.%d:", c);
        if (node->els)
        {
            gen_stmt(node->els);
        }
        println(".L.end.%d:", c);
        return;
    }
    case ND_LOOP:
    {
        int c = count();
        if (node->init)
        {
            gen_stmt(node->init);
        }
        println(".L.begin.%d:", c);
        if (node->cond)
        {
            gen_expr(node->cond);
            println("  cmp $0, %%rax");
            println("  je %s", node->brk_label);
        }
        gen_stmt(node->then);
        println("%s:", node->cont_label);
        if (node->inc)
        {
            gen_expr(node->inc);
        }
        println("  jmp .L.begin.%d", c);
        println("%s:", node->brk_label);
        return;
    }
    case ND_DO:
    {
        int c = count();
        println(".L.begin.%d:", c);
        gen_stmt(node->then);
        println("%s:", node->cont_label);
        gen_expr(node->cond);
        println("  cmp $0, %%rax");
        println("  jne .L.begin.%d", c);
        println("%s:", node->brk_label);
        return;
    }
    case ND_SWITCH:
        gen_expr(node->cond);

        for (Node *n = node->case_next; n; n = n->case_next)
        {
            char *reg = (node->cond->ty->size == 8) ? "%rax" : "%eax";
            println("  cmp $%ld, %s", n->val, reg);
            println("  je %s", n->label);
        }

        if (node->default_case)
        {
            println("  jmp %s", node->default_case->label);
        }

        println("  jmp %s", node->brk_label);
        gen_stmt(node->then);
        println("%s:", node->brk_label);
        return;
    case ND_CASE:
        println("%s:", node->label);
        gen_stmt(node->lhs);
        return;
    case ND_BLOCK:
        for (Node *n = node->body; n; n = n->next)
        {
            gen_stmt(n);
        }
        return;
    case ND_GOTO:
        println("  jmp %s", node->unique_label);
        return;
    case ND_LABEL:
        println("%s:", node->unique_label);
        gen_stmt(node->lhs);
        return;
    case ND_RETURN:
        if (node->lhs)
        {
            gen_expr(node->lhs);
        }
        println("  jmp .L.return.%s", current_fn->name);
        return;
    case ND_EXPR_STMT:
        gen_expr(node->lhs);
        return;
    }

    error_tok(node->tok, "invalid statement");
}

// Assign offsets to local variables
static void assign_lvar_offsets(Var *prog)
{
    for (Var *fn = prog; fn; fn = fn->next)
    {
        if (!fn->is_function)
        {
            continue;
        }
        int offset = 0;
        for (Var *var = fn->locals; var; var = var->next)
        {
            offset += var->ty->size;
            offset = align_to(offset, var->align);
            var->offset = -offset;
        }
        fn->stack_size = align_to(offset, 16);
    }
}

static void emit_data(Var *prog)
{
    for (Var *var = prog; var; var = var->next)
    {
        if (var->is_function || !var->is_definition)
        {
            continue;
        }

        if (var->is_static)
        {
            println("  .local %s", var->name);
        }
        else
        {
            println("  .globl %s", var->name);
        }
        println("  .align %d", var->align);

        if (var->init_data)
        {
            println("  .data");
            println("%s:", var->name);

            Relocation *rel = var->rel;
            int pos = 0;
            while (pos < var->ty->size)
            {
                if (rel && rel->offset == pos)
                {
                    println("  .quad %s%+ld", rel->label, rel->addend);
                    rel = rel->next;
                    pos += 8;
                }
                else
                {
                    println("  .byte %d", var->init_data[pos++]);
                }
            }
            continue;
        }
        println("  .bss");
        println("%s:", var->name);
        println("  .zero %d", var->ty->size);
    }
}

static void store_gp(int r, int offset, int sz)
{
    switch (sz)
    {
    case 1:
        println("  mov %s, %d(%%rbp)", argreg8[r], offset);
        return;
    case 2:
        println("  mov %s, %d(%%rbp)", argreg16[r], offset);
        return;
    case 4:
        println("  mov %s, %d(%%rbp)", argreg32[r], offset);
        return;
    case 8:
        println("  mov %s, %d(%%rbp)", argreg64[r], offset);
        return;
    }
    unreachable();
}

static void emit_text(Var *prog)
{
    for (Var *fn = prog; fn; fn = fn->next)
    {
        if (!fn->is_function || !fn->is_definition)
        {
            continue;
        }

        if (fn->is_static)
        {
            println("  .local %s", fn->name);
        }
        else
        {
            println("  .globl %s", fn->name);
        }
        println("  .text");
        println("%s:", fn->name);
        current_fn = fn;

        // Prologue
        println("  push %%rbp");
        println("  mov %%rsp, %%rbp");
        println("  sub $%d, %%rsp", fn->stack_size);

        // Save arg registers if function is variadic
        if (fn->va_area)
        {
            int gp = 0;
            for (Var *var = fn->params; var; var = var->next)
            {
                gp++;
            }
            int off = fn->va_area->offset;

            // va_elem
            println("  movl $%d, %d(%%rbp)", gp * 8, off);
            println("  movl $0, %d(%%rbp)", off + 4);
            println("  movq %%rbp, %d(%%rbp)", off + 16);
            println("  addq $%d, %d(%%rbp)", off + 24, off + 16);

            // __reg_save_area__
            println("  movq %%rdi, %d(%%rbp)", off + 24);
            println("  movq %%rsi, %d(%%rbp)", off + 32);
            println("  movq %%rdx, %d(%%rbp)", off + 40);
            println("  movq %%rcx, %d(%%rbp)", off + 48);
            println("  movq %%r8, %d(%%rbp)", off + 56);
            println("  movq %%r9, %d(%%rbp)", off + 64);
            println("  movsd %%xmm0, %d(%%rbp)", off + 72);
            println("  movsd %%xmm1, %d(%%rbp)", off + 80);
            println("  movsd %%xmm2, %d(%%rbp)", off + 88);
            println("  movsd %%xmm3, %d(%%rbp)", off + 96);
            println("  movsd %%xmm4, %d(%%rbp)", off + 104);
            println("  movsd %%xmm5, %d(%%rbp)", off + 112);
            println("  movsd %%xmm6, %d(%%rbp)", off + 120);
            println("  movsd %%xmm7, %d(%%rbp)", off + 128);
        }

        // Save passed-by-register arguments to the stack
        int i = 0;
        for (Var *var = fn->params; var; var = var->next)
        {
            store_gp(i++, var->offset, var->ty->size);
        }

        // Emit code
        gen_stmt(fn->body);
        assert(depth == 0);

        // Epilogue
        println(".L.return.%s:", fn->name);
        println("  mov %%rbp, %%rsp");
        println("  pop %%rbp");
        println("  ret");
    }
}

void codegen(Var *prog, FILE *out)
{
    output_file = out;
    assign_lvar_offsets(prog);
    emit_data(prog);
    emit_text(prog);
}