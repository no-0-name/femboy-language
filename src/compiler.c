#include "compiler.h"
#include "builtins.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#define MAX_LOCALS 256

typedef struct {
    char *name;
} Local;

typedef struct FuncInfo {
    char *name;
    int arity;
    int code_start;
    struct FuncInfo *next;
} FuncInfo;

typedef struct PendingCall {
    int operand_pos;
    FuncInfo *target;
    struct PendingCall *next;
} PendingCall;

typedef struct PendingBreak {
    int jump_operand_pos;
    struct PendingBreak *next;
} PendingBreak;

typedef struct LoopContext {
    int continue_target;

    bool continue_target_known;

    int handler_depth_at_start;
    PendingBreak *pending_breaks;
    PendingBreak *pending_continues;
    struct LoopContext *enclosing;
} LoopContext;

typedef struct {
    Chunk *chunk;
    Local locals[MAX_LOCALS];
    int nlocals;
    bool in_function;
    FuncInfo *functions;
    PendingCall *pending;
    LoopContext *loop;
    int try_depth;
    bool failed;
    FemboyError *err;
} CompilerState;

static void compile_fail(CompilerState *cs, int line, const char *fmt, ...) {
    if (cs->failed) return;
    cs->failed = true;
    cs->err->status = FEMBOY_ERR_COMPILE;
    cs->err->line = line;
    cs->err->column = -1;
    va_list args;
    va_start(args, fmt);
    vsnprintf(cs->err->message, FEMBOY_ERR_MSG_MAX, fmt, args);
    va_end(args);
}

static FuncInfo *find_func(CompilerState *cs, const char *name) {
    for (FuncInfo *f = cs->functions; f; f = f->next)
        if (!strcmp(f->name, name)) return f;
    return NULL;
}

static int resolve_local(CompilerState *cs, const char *name) {
    for (int i = cs->nlocals - 1; i >= 0; i--)
        if (!strcmp(cs->locals[i].name, name)) return i;
    return -1;
}

static void compile_node(CompilerState *cs, Node *n);

static void compile_expr(CompilerState *cs, Node *n) {
    if (cs->failed) return;
    switch (n->type) {
        case N_NUMBER: {
            int idx = chunk_add_const_num(cs->chunk, n->num);
            chunk_emit_byte(cs->chunk, OP_PUSH_NUM);
            chunk_emit_u16(cs->chunk, (uint16_t)idx);
            break;
        }
        case N_STRING: {
            int idx = chunk_add_const_str(cs->chunk, n->str);
            chunk_emit_byte(cs->chunk, OP_PUSH_STR);
            chunk_emit_u16(cs->chunk, (uint16_t)idx);
            break;
        }
        case N_BOOL:
            chunk_emit_byte(cs->chunk, OP_PUSH_BOOL);
            chunk_emit_byte(cs->chunk, n->boolean ? 1 : 0);
            break;
        case N_NIL:
            chunk_emit_byte(cs->chunk, OP_PUSH_NIL);
            break;
        case N_VAR: {
            int slot = cs->in_function ? resolve_local(cs, n->name) : -1;
            if (slot >= 0) {
                chunk_emit_byte(cs->chunk, OP_GET_LOCAL);
                chunk_emit_u16(cs->chunk, (uint16_t)slot);
            } else {
                int idx = chunk_add_const_str(cs->chunk, n->name);
                chunk_emit_byte(cs->chunk, OP_GET_GLOBAL);
                chunk_emit_u16(cs->chunk, (uint16_t)idx);
            }
            break;
        }
        case N_ASSIGN: {
            compile_expr(cs, n->init);
            if (cs->failed) return;
            int slot = cs->in_function ? resolve_local(cs, n->name) : -1;
            if (slot >= 0) {
                chunk_emit_byte(cs->chunk, OP_SET_LOCAL);
                chunk_emit_u16(cs->chunk, (uint16_t)slot);
            } else {
                int idx = chunk_add_const_str(cs->chunk, n->name);
                chunk_emit_byte(cs->chunk, OP_SET_GLOBAL);
                chunk_emit_u16(cs->chunk, (uint16_t)idx);
            }
            break;
        }
        case N_UNOP: {
            compile_expr(cs, n->left);
            if (cs->failed) return;
            if (n->op == T_MINUS) chunk_emit_byte(cs->chunk, OP_NEG);
            else if (n->op == T_BANG) chunk_emit_byte(cs->chunk, OP_NOT);
            break;
        }
        case N_BINOP: {
            if (n->op == T_AND) {
                compile_expr(cs, n->left);
                if (cs->failed) return;
                chunk_emit_byte(cs->chunk, OP_JUMP_IF_FALSE);
                int jmp = cs->chunk->count; chunk_emit_u16(cs->chunk, 0);
                chunk_emit_byte(cs->chunk, OP_POP);
                compile_expr(cs, n->right);
                if (cs->failed) return;
                int end = cs->chunk->count;
                cs->chunk->code[jmp] = (end - jmp - 2) & 0xFF;
                cs->chunk->code[jmp + 1] = ((end - jmp - 2) >> 8) & 0xFF;
                break;
            }
            if (n->op == T_OR) {
                compile_expr(cs, n->left);
                if (cs->failed) return;
                chunk_emit_byte(cs->chunk, OP_JUMP_IF_FALSE);
                int elsej = cs->chunk->count; chunk_emit_u16(cs->chunk, 0);
                chunk_emit_byte(cs->chunk, OP_JUMP);
                int endj = cs->chunk->count; chunk_emit_u16(cs->chunk, 0);
                int elsepos = cs->chunk->count;
                cs->chunk->code[elsej] = (elsepos - elsej - 2) & 0xFF;
                cs->chunk->code[elsej + 1] = ((elsepos - elsej - 2) >> 8) & 0xFF;
                chunk_emit_byte(cs->chunk, OP_POP);
                compile_expr(cs, n->right);
                if (cs->failed) return;
                int end = cs->chunk->count;
                cs->chunk->code[endj] = (end - endj - 2) & 0xFF;
                cs->chunk->code[endj + 1] = ((end - endj - 2) >> 8) & 0xFF;
                break;
            }
            compile_expr(cs, n->left);
            if (cs->failed) return;
            compile_expr(cs, n->right);
            if (cs->failed) return;
            switch (n->op) {
                case T_PLUS: chunk_emit_byte(cs->chunk, OP_ADD); break;
                case T_MINUS: chunk_emit_byte(cs->chunk, OP_SUB); break;
                case T_STAR: chunk_emit_byte(cs->chunk, OP_MUL); break;
                case T_SLASH: chunk_emit_byte(cs->chunk, OP_DIV); break;
                case T_PERCENT: chunk_emit_byte(cs->chunk, OP_MOD); break;
                case T_EQEQ: chunk_emit_byte(cs->chunk, OP_EQ); break;
                case T_BANGEQ: chunk_emit_byte(cs->chunk, OP_NEQ); break;
                case T_LT: chunk_emit_byte(cs->chunk, OP_LT); break;
                case T_LE: chunk_emit_byte(cs->chunk, OP_LE); break;
                case T_GT: chunk_emit_byte(cs->chunk, OP_GT); break;
                case T_GE: chunk_emit_byte(cs->chunk, OP_GE); break;
                default: compile_fail(cs, n->line, "unknown binary operator"); return;
            }
            break;
        }
        case N_ARRAY_LIT: {
            for (int i = 0; i < n->nargs; i++) {
                compile_expr(cs, n->args[i]);
                if (cs->failed) return;
            }
            chunk_emit_byte(cs->chunk, OP_MAKE_ARRAY);
            chunk_emit_u16(cs->chunk, (uint16_t)n->nargs);
            break;
        }
        case N_MAP_LIT: {

            for (int i = 0; i < n->nargs; i++) {
                compile_expr(cs, n->args[i]);
                if (cs->failed) return;
            }
            chunk_emit_byte(cs->chunk, OP_MAKE_MAP);
            chunk_emit_u16(cs->chunk, (uint16_t)n->nargs);
            for (int i = 0; i < n->nargs; i++) {
                int idx = chunk_add_const_str(cs->chunk, n->keys[i]);
                chunk_emit_u16(cs->chunk, (uint16_t)idx);
            }
            break;
        }
        case N_INDEX_GET: {
            compile_expr(cs, n->left);
            if (cs->failed) return;
            compile_expr(cs, n->right);
            if (cs->failed) return;
            chunk_emit_byte(cs->chunk, OP_INDEX_GET);
            break;
        }
        case N_INDEX_SET: {

            if (n->left->type != N_VAR) {
                compile_fail(cs, n->line,
                             "indexed assignment is only supported for a simple variable "
                             "(e.g. 'a[i] = x'); nested indices like 'a[i][j] = x' are not supported");
                return;
            }
            const char *varname = n->left->name;
            int slot = cs->in_function ? resolve_local(cs, varname) : -1;
            bool is_local = (slot >= 0);

            compile_expr(cs, n->right);
            if (cs->failed) return;
            compile_expr(cs, n->value);
            if (cs->failed) return;

            if (is_local) {
                chunk_emit_byte(cs->chunk, OP_INDEX_SET_LOCAL);
                chunk_emit_u16(cs->chunk, (uint16_t)slot);
            } else {
                int idx = chunk_add_const_str(cs->chunk, varname);
                chunk_emit_byte(cs->chunk, OP_INDEX_SET_GLOBAL);
                chunk_emit_u16(cs->chunk, (uint16_t)idx);
            }
            break;
        }
        case N_CALL: {

            if (!strcmp(n->name, "len")) {
                if (n->nargs != 1) {
                    compile_fail(cs, n->line, "function 'len' expects 1 argument, got %d", n->nargs);
                    return;
                }
                compile_expr(cs, n->args[0]);
                if (cs->failed) return;
                chunk_emit_byte(cs->chunk, OP_ARRAY_LEN);
                break;
            }
            if (!strcmp(n->name, "push")) {
                if (n->nargs != 2) {
                    compile_fail(cs, n->line, "function 'push' expects 2 arguments (array, element), got %d", n->nargs);
                    return;
                }
                compile_expr(cs, n->args[0]);
                if (cs->failed) return;
                compile_expr(cs, n->args[1]);
                if (cs->failed) return;
                chunk_emit_byte(cs->chunk, OP_ARRAY_PUSH);
                break;
            }
            if (!strcmp(n->name, "has")) {
                if (n->nargs != 2) {
                    compile_fail(cs, n->line, "function 'has' expects 2 arguments (map, key), got %d", n->nargs);
                    return;
                }
                compile_expr(cs, n->args[0]);
                if (cs->failed) return;
                compile_expr(cs, n->args[1]);
                if (cs->failed) return;
                chunk_emit_byte(cs->chunk, OP_MAP_HAS);
                break;
            }
            if (!strcmp(n->name, "keys")) {
                if (n->nargs != 1) {
                    compile_fail(cs, n->line, "function 'keys' expects 1 argument, got %d", n->nargs);
                    return;
                }
                compile_expr(cs, n->args[0]);
                if (cs->failed) return;
                chunk_emit_byte(cs->chunk, OP_MAP_KEYS);
                break;
            }
            if (!strcmp(n->name, "delete")) {
                if (n->nargs != 2) {
                    compile_fail(cs, n->line, "function 'delete' expects 2 arguments (map, key), got %d", n->nargs);
                    return;
                }
                compile_expr(cs, n->args[0]);
                if (cs->failed) return;
                compile_expr(cs, n->args[1]);
                if (cs->failed) return;
                chunk_emit_byte(cs->chunk, OP_MAP_DELETE);
                break;
            }

            {
                BuiltinId bid;
                int barity;
                if (builtin_lookup(n->name, &bid, &barity)) {
                    if (n->nargs != barity) {
                        compile_fail(cs, n->line, "function '%s' expects %d argument(s), got %d",
                                     n->name, barity, n->nargs);
                        return;
                    }
                    for (int i = 0; i < n->nargs; i++) {
                        compile_expr(cs, n->args[i]);
                        if (cs->failed) return;
                    }
                    chunk_emit_byte(cs->chunk, builtin_needs_stack_protection(bid)
                                                    ? OP_CALL_BUILTIN_MULTI : OP_CALL_BUILTIN);
                    chunk_emit_byte(cs->chunk, (uint8_t)bid);
                    chunk_emit_byte(cs->chunk, (uint8_t)n->nargs);
                    break;
                }
            }

            FuncInfo *f = find_func(cs, n->name);
            if (!f) { compile_fail(cs, n->line, "call to unknown function '%s'", n->name); return; }
            if (f->arity != n->nargs) {
                compile_fail(cs, n->line, "function '%s' expects %d argument(s), got %d",
                             n->name, f->arity, n->nargs);
                return;
            }
            for (int i = 0; i < n->nargs; i++) {
                compile_expr(cs, n->args[i]);
                if (cs->failed) return;
            }
            chunk_emit_byte(cs->chunk, OP_CALL);
            int operand_pos = cs->chunk->count;
            chunk_emit_u16(cs->chunk, f->code_start >= 0 ? (uint16_t)f->code_start : 0);
            if (f->code_start < 0) {
                PendingCall *pc = femboy_malloc(sizeof(PendingCall));
                pc->operand_pos = operand_pos;
                pc->target = f;
                pc->next = cs->pending;
                cs->pending = pc;
            }
            chunk_emit_byte(cs->chunk, (uint8_t)n->nargs);
            break;
        }
        default:
            compile_fail(cs, n->line, "expected an expression during compilation");
            return;
    }
}

static int emit_jump(CompilerState *cs, OpCode op) {
    chunk_emit_byte(cs->chunk, op);
    int pos = cs->chunk->count;
    chunk_emit_u16(cs->chunk, 0xFFFF);
    return pos;
}

static void patch_jump(CompilerState *cs, int pos) {
    int dest = cs->chunk->count;
    int offset = dest - pos - 2;
    cs->chunk->code[pos] = offset & 0xFF;
    cs->chunk->code[pos + 1] = (offset >> 8) & 0xFF;
}

static void compile_block(CompilerState *cs, Node *blk) {
    if (cs->failed) return;
    int saved_nlocals = cs->nlocals;
    for (int i = 0; i < blk->nstmts; i++) {
        compile_node(cs, blk->stmts[i]);
        if (cs->failed) return;
    }
    cs->nlocals = saved_nlocals;
}

static void compile_node(CompilerState *cs, Node *n) {
    if (cs->failed) return;
    switch (n->type) {
        case N_LET: {
            if (n->init) compile_expr(cs, n->init); else chunk_emit_byte(cs->chunk, OP_PUSH_NIL);
            if (cs->failed) return;
            if (cs->in_function) {
                if (cs->nlocals >= MAX_LOCALS) {
                    compile_fail(cs, n->line, "too many local variables in function (maximum %d)", MAX_LOCALS);
                    return;
                }
                cs->locals[cs->nlocals].name = n->name;
                int slot = cs->nlocals++;
                chunk_emit_byte(cs->chunk, OP_DEFINE_LOCAL);
                chunk_emit_u16(cs->chunk, (uint16_t)slot);
            } else {
                int idx = chunk_add_const_str(cs->chunk, n->name);
                chunk_emit_byte(cs->chunk, OP_DEFINE_GLOBAL);
                chunk_emit_u16(cs->chunk, (uint16_t)idx);
            }
            break;
        }
        case N_PRINT:
            compile_expr(cs, n->init);
            if (cs->failed) return;
            chunk_emit_byte(cs->chunk, OP_PRINT);
            break;
        case N_EXPRSTMT:
            compile_expr(cs, n->init);
            if (cs->failed) return;
            chunk_emit_byte(cs->chunk, OP_POP);
            break;
        case N_IF: {
            compile_expr(cs, n->cond);
            if (cs->failed) return;
            int thenJump = emit_jump(cs, OP_JUMP_IF_FALSE);
            chunk_emit_byte(cs->chunk, OP_POP);
            compile_block(cs, n->thenb);
            if (cs->failed) return;
            int elseJump = emit_jump(cs, OP_JUMP);
            patch_jump(cs, thenJump);
            chunk_emit_byte(cs->chunk, OP_POP);
            if (n->elseb) {
                if (n->elseb->type == N_BLOCK) compile_block(cs, n->elseb);
                else compile_node(cs, n->elseb);
                if (cs->failed) return;
            }
            patch_jump(cs, elseJump);
            break;
        }
        case N_WHILE: {
            LoopContext loop = { .pending_breaks = NULL, .pending_continues = NULL, .enclosing = cs->loop, .handler_depth_at_start = cs->try_depth };
            cs->loop = &loop;

            int loopStart = cs->chunk->count;
            loop.continue_target = loopStart;
            loop.continue_target_known = true;
            compile_expr(cs, n->cond);
            if (cs->failed) { cs->loop = loop.enclosing; return; }
            int exitJump = emit_jump(cs, OP_JUMP_IF_FALSE);
            chunk_emit_byte(cs->chunk, OP_POP);
            compile_block(cs, n->body);
            if (cs->failed) { cs->loop = loop.enclosing; return; }
            int back = emit_jump(cs, OP_JUMP);
            int offset = loopStart - back - 2;
            cs->chunk->code[back] = offset & 0xFF;
            cs->chunk->code[back + 1] = (offset >> 8) & 0xFF;
            patch_jump(cs, exitJump);
            chunk_emit_byte(cs->chunk, OP_POP);

            for (PendingBreak *pb = loop.pending_breaks; pb; ) {
                patch_jump(cs, pb->jump_operand_pos);
                PendingBreak *next = pb->next;
                free(pb);
                pb = next;
            }
            cs->loop = loop.enclosing;
            break;
        }
        case N_FOR: {

            int saved_nlocals = cs->nlocals;

            if (n->init) {
                if (n->init->type != N_LET && n->init->type != N_EXPRSTMT) {
                    compile_fail(cs, n->line,
                                 "'for' initializer must be a variable declaration or an expression");
                    return;
                }
                compile_node(cs, n->init);
                if (cs->failed) { cs->nlocals = saved_nlocals; return; }
            }

            LoopContext loop = { .pending_breaks = NULL, .pending_continues = NULL, .enclosing = cs->loop, .handler_depth_at_start = cs->try_depth };
            loop.continue_target_known = false;
            cs->loop = &loop;

            int loopStart = cs->chunk->count;
            int exitJump;
            if (n->cond) {
                compile_expr(cs, n->cond);
                if (cs->failed) { cs->loop = loop.enclosing; cs->nlocals = saved_nlocals; return; }
                exitJump = emit_jump(cs, OP_JUMP_IF_FALSE);
                chunk_emit_byte(cs->chunk, OP_POP);
            } else {
                exitJump = -1;
            }

            compile_block(cs, n->body);
            if (cs->failed) { cs->loop = loop.enclosing; cs->nlocals = saved_nlocals; return; }

            loop.continue_target = cs->chunk->count;
            loop.continue_target_known = true;
            for (PendingBreak *pc = loop.pending_continues; pc; ) {
                patch_jump(cs, pc->jump_operand_pos);
                PendingBreak *next = pc->next;
                free(pc);
                pc = next;
            }
            loop.pending_continues = NULL;
            if (n->step) {
                compile_expr(cs, n->step);
                if (cs->failed) { cs->loop = loop.enclosing; cs->nlocals = saved_nlocals; return; }
                chunk_emit_byte(cs->chunk, OP_POP);
            }

            int back = emit_jump(cs, OP_JUMP);
            int offset = loopStart - back - 2;
            cs->chunk->code[back] = offset & 0xFF;
            cs->chunk->code[back + 1] = (offset >> 8) & 0xFF;

            if (exitJump >= 0) {
                patch_jump(cs, exitJump);
                chunk_emit_byte(cs->chunk, OP_POP);
            }

            for (PendingBreak *pb = loop.pending_breaks; pb; ) {
                patch_jump(cs, pb->jump_operand_pos);
                PendingBreak *next = pb->next;
                free(pb);
                pb = next;
            }
            cs->loop = loop.enclosing;
            cs->nlocals = saved_nlocals;
            break;
        }
        case N_BREAK: {
            if (!cs->loop) {
                compile_fail(cs, n->line, "'break' outside a loop");
                return;
            }
            int extra_handlers = cs->try_depth - cs->loop->handler_depth_at_start;
            if (extra_handlers > 0) {
                chunk_emit_byte(cs->chunk, OP_POP_HANDLERS);
                chunk_emit_byte(cs->chunk, (uint8_t)extra_handlers);
            }
            int jumpPos = emit_jump(cs, OP_JUMP);
            PendingBreak *pb = femboy_malloc(sizeof(PendingBreak));
            pb->jump_operand_pos = jumpPos;
            pb->next = cs->loop->pending_breaks;
            cs->loop->pending_breaks = pb;
            break;
        }
        case N_CONTINUE: {
            if (!cs->loop) {
                compile_fail(cs, n->line, "'continue' outside a loop");
                return;
            }
            int extra_handlers = cs->try_depth - cs->loop->handler_depth_at_start;
            if (extra_handlers > 0) {
                chunk_emit_byte(cs->chunk, OP_POP_HANDLERS);
                chunk_emit_byte(cs->chunk, (uint8_t)extra_handlers);
            }
            if (cs->loop->continue_target_known) {

                chunk_emit_byte(cs->chunk, OP_JUMP);
                int offset = cs->loop->continue_target - cs->chunk->count - 2;
                chunk_emit_u16(cs->chunk, (uint16_t)offset);
            } else {

                int jumpPos = emit_jump(cs, OP_JUMP);
                PendingBreak *pc = femboy_malloc(sizeof(PendingBreak));
                pc->jump_operand_pos = jumpPos;
                pc->next = cs->loop->pending_continues;
                cs->loop->pending_continues = pc;
            }
            break;
        }
        case N_TRY: {

            int tryBeginPos = emit_jump(cs, OP_TRY_BEGIN);

            cs->try_depth++;
            compile_block(cs, n->thenb);
            cs->try_depth--;
            if (cs->failed) return;
            chunk_emit_byte(cs->chunk, OP_TRY_END);
            int jumpOverCatch = emit_jump(cs, OP_JUMP);

            int catchAddr = cs->chunk->count;
            cs->chunk->code[tryBeginPos] = (catchAddr - tryBeginPos - 2) & 0xFF;
            cs->chunk->code[tryBeginPos + 1] = ((catchAddr - tryBeginPos - 2) >> 8) & 0xFF;

            int saved_nlocals = cs->nlocals;
            if (cs->in_function) {
                if (cs->nlocals >= MAX_LOCALS) {
                    compile_fail(cs, n->line, "too many local variables in function (maximum %d)", MAX_LOCALS);
                    return;
                }
                cs->locals[cs->nlocals].name = n->name;
                int slot = cs->nlocals++;
                chunk_emit_byte(cs->chunk, OP_DEFINE_LOCAL);
                chunk_emit_u16(cs->chunk, (uint16_t)slot);
            } else {
                int idx = chunk_add_const_str(cs->chunk, n->name);
                chunk_emit_byte(cs->chunk, OP_DEFINE_GLOBAL);
                chunk_emit_u16(cs->chunk, (uint16_t)idx);
            }

            for (int i = 0; i < n->elseb->nstmts; i++) {
                compile_node(cs, n->elseb->stmts[i]);
                if (cs->failed) return;
            }
            cs->nlocals = saved_nlocals;

            patch_jump(cs, jumpOverCatch);
            break;
        }
        case N_THROW: {
            compile_expr(cs, n->init);
            if (cs->failed) return;
            chunk_emit_byte(cs->chunk, OP_THROW);
            break;
        }
        case N_RETURN:
            if (n->init) compile_expr(cs, n->init); else chunk_emit_byte(cs->chunk, OP_PUSH_NIL);
            if (cs->failed) return;
            chunk_emit_byte(cs->chunk, OP_RETURN);
            break;
        case N_BLOCK:
            compile_block(cs, n);
            break;
        case N_FUNCDEF:

            break;
        default:
            compile_expr(cs, n);
            if (cs->failed) return;
            chunk_emit_byte(cs->chunk, OP_POP);
    }
}

FemboyStatus femboy_compile(Node *prog, Chunk *chunk, FemboyError *err) {
    CompilerState cs = {0};
    cs.chunk = chunk;
    cs.nlocals = 0;
    cs.in_function = false;
    cs.functions = NULL;
    cs.pending = NULL;
    cs.loop = NULL;
    cs.try_depth = 0;
    cs.failed = false;
    cs.err = err;

    for (int i = 0; i < prog->nstmts; i++) {
        Node *s = prog->stmts[i];
        if (s->type == N_FUNCDEF) {
            BuiltinId reserved_id;
            int reserved_arity;
            if (!strcmp(s->name, "len") || !strcmp(s->name, "push") ||
                !strcmp(s->name, "has") || !strcmp(s->name, "keys") || !strcmp(s->name, "delete") ||
                builtin_lookup(s->name, &reserved_id, &reserved_arity)) {
                compile_fail(&cs, s->line, "name '%s' is reserved by a built-in function", s->name);
                break;
            }
            if (find_func(&cs, s->name)) {
                compile_fail(&cs, s->line, "function '%s' is already defined", s->name);
                break;
            }
            FuncInfo *f = femboy_malloc(sizeof(FuncInfo));
            f->name = s->name;
            f->arity = s->nparams;
            f->code_start = -1;
            f->next = cs.functions;
            cs.functions = f;
        }
    }

    if (!cs.failed) {
        for (int i = 0; i < prog->nstmts; i++) {
            if (prog->stmts[i]->type != N_FUNCDEF) {
                compile_node(&cs, prog->stmts[i]);
                if (cs.failed) break;
            }
        }
    }
    if (!cs.failed) chunk_emit_byte(chunk, OP_HALT);

    if (!cs.failed) {
        for (int i = 0; i < prog->nstmts; i++) {
            Node *s = prog->stmts[i];
            if (s->type != N_FUNCDEF) continue;

            FuncInfo *f = find_func(&cs, s->name);
            f->code_start = chunk->count;

            cs.in_function = true;
            cs.nlocals = 0;
            cs.loop = NULL;
            cs.try_depth = 0;
            for (int p = 0; p < s->nparams; p++) {
                cs.locals[cs.nlocals].name = s->params[p];
                cs.nlocals++;
            }
            compile_block(&cs, s->fbody);
            if (cs.failed) break;

            chunk_emit_byte(chunk, OP_PUSH_NIL);
            chunk_emit_byte(chunk, OP_RETURN);
            cs.in_function = false;
        }
    }

    if (!cs.failed) {
        for (PendingCall *pc = cs.pending; pc; pc = pc->next) {
            if (pc->target->code_start < 0) {
                compile_fail(&cs, -1, "internal compiler error: function '%s' was not compiled", pc->target->name);
                break;
            }
            uint16_t addr = (uint16_t)pc->target->code_start;
            chunk->code[pc->operand_pos] = addr & 0xFF;
            chunk->code[pc->operand_pos + 1] = (addr >> 8) & 0xFF;
        }
    }

    for (FuncInfo *f = cs.functions; f; ) { FuncInfo *next = f->next; free(f); f = next; }
    for (PendingCall *pc = cs.pending; pc; ) { PendingCall *next = pc->next; free(pc); pc = next; }

    return cs.failed ? FEMBOY_ERR_COMPILE : FEMBOY_OK;
}
