#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include "cc.h"

// =============================================================================
// Count identifiers in an AST.
// =============================================================================
static void idents_in_statement(const Node *node, Map *idents) {
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
    if (node->ty == '=' && node->lhs && node->lhs->ty == ND_IDENT) {
        map_put(idents, node->lhs->name, (void *)(-8 * (idents->keys->len+1)));
    }
    if (node->lhs) idents_in_statement(node->lhs, idents);
    if (node->rhs) idents_in_statement(node->rhs, idents);
}

Map *idents_in_code(const Vector *code) {
    Map *idents = new_map();
    Node **current = (Node **)code->data;
    while (*current) {
        idents_in_statement(*current, idents);
        ++current;
    }
    return idents;
}

// Track stack position for adjusting alignment.
static int stackpos = 0;

// =============================================================================
// Assembly generation from an AST.
// =============================================================================
static void push_imm32(int imm) {
    printf("  push %d\n", imm);
    stackpos += 8;
}

static void push(const char *reg) {
    printf("  push %s\n", reg);
    stackpos += 8;
}

static void pop(const char *reg) {
    printf("  pop %s\n", reg);
    stackpos -= 8;
    assert(stackpos >= 0);
}

void gen_lval(Node *node, const Map *idents) {
#pragma GCC diagnostic ignored "-Wpointer-to-int-cast"
    if (node->ty != ND_IDENT) {
        fprintf(stderr, "Not an identifier.\n");
        exit(1);
    }

    int offset = (int)map_get(idents, node->name);
    printf("  lea rax, [rbp%+d]\n", offset);
    push("rax");
}

void gen(Node *node, const Map *idents) {
    switch (node->ty) {
    case ND_NUM:
        push_imm32(node->val);
        return;

    case ND_IDENT:
        gen_lval(node, idents);
        pop("rax");
        printf("  mov rax, [rax]\n");
        push("rax");
        return;

    case ND_CALL: {
        int nargs = node->args->len;
        int nregargs = nargs <= 6 ? nargs : 6;
        int nstackargs = nargs - nregargs;
        char *regs[] = { "rdi", "rsi", "rdx", "rcx", "r8", "r9" };

        // Align stack pointer to 16 bytes.
        int orig_stackpos = stackpos;
        bool align_stack = (stackpos + 8 * nstackargs) % 16 != 0;
        if (align_stack) {
            printf("  sub rsp, 8\n");
            stackpos += 8;
        }

        // Evaluate argument expressions.
        for (int i = nargs - 1; i >= 0; i--)
            gen(node->args->data[i], idents);

        // Assign first 6 args to registers. Leave the rest on the stack.
        for (int i = 0; i < nregargs; i++)
            pop(regs[i]);

        printf("  xor rax, rax\n");
        printf("  call %s\n", node->name);

        // Remove stack-passed args.
        if (nstackargs > 0) {
            printf("  sub rsp, %d\n", 8 * nstackargs);
            stackpos -= 8 * nstackargs;
        }

        if (align_stack) {
            printf("  add rsp, 8\n");
            stackpos -= 8;
        }
        assert(stackpos == orig_stackpos);

        push("rax");
        assert(true);
        return;
    }

    case '=':
        gen_lval(node->lhs, idents);
        gen(node->rhs, idents);

        pop("rdi");
        pop("rax");
        printf("  mov [rax], rdi\n");
        push("rdi");
        return;
    }

    gen(node->lhs, idents);
    gen(node->rhs, idents);

    pop("rdi");
    pop("rax");

    switch (node->ty) {
    case '+':
        printf("  add rax, rdi\n");
        break;
    case '-':
        printf("  sub rax, rdi\n");
        break;
    case '*':
        printf("  mul rdi\n");
        break;
    case '/':
        printf("  xor rdx, rdx\n");
        printf("  div rdi\n");
        break;
    case ND_EQUAL:
        printf("  cmp rax, rdi\n");
        printf("  sete al\n");
        printf("  movzb rax, al\n");
        break;
    case ND_NOTEQUAL:
        printf("  cmp rax, rdi\n");
        printf("  setne al\n");
        printf("  movzb rax, al\n");
        break;
    default:
        fprintf(stderr, "An unexpected operator type %d during assembly generation.\n",
                node->ty);
        exit(1);
    }

    push("rax");
}
