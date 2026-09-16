/*
 * spl.c  --  SPL (Sparse Pragmatic Language) interpreter
 * Standard: C99,  Compiler target: tdm-gcc (MinGW)
 *
 * Build:  gcc -std=c99 -Wall -Wextra -o spl.exe spl.c
 * Run:    spl.exe script.spl
 *
 * Supported keywords (every keyword is exactly 4 bytes):
 *   varm  varl  set_  add_  sub_  mul_  div_  mod_
 *   prnt  inpt  if__  elif  else  end_  loop  brk_
 *   func  retn  call  labl  goto
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ------------------------------------------------------------------ */
/*  Limits                                                             */
/* ------------------------------------------------------------------ */
#define MAX_LINES     4096
#define MAX_LINE_LEN  1024
#define MAX_VARS      512
#define MAX_FUNCS     128
#define MAX_LABELS    128
#define MAX_CALL_DEPTH 64
#define MAX_PARAMS    8

/* ------------------------------------------------------------------ */
/*  Value                                                              */
/* ------------------------------------------------------------------ */
typedef enum { VT_INT, VT_FLT, VT_STR } ValType;

typedef struct {
    ValType type;
    long long  ival;
    double     fval;
    char       sval[256];
} Val;

static Val val_int(long long v)    { Val r; r.type=VT_INT; r.ival=v; r.fval=0; r.sval[0]=0; return r; }
static Val val_flt(double v)       { Val r; r.type=VT_FLT; r.ival=0; r.fval=v; r.sval[0]=0; return r; }
static Val val_str(const char *s)  { Val r; r.type=VT_STR; r.ival=0; r.fval=0; strncpy(r.sval,s,255); r.sval[255]=0; return r; }

static double val_to_double(Val v) {
    if (v.type == VT_INT) return (double)v.ival;
    if (v.type == VT_FLT) return v.fval;
    return 0.0;
}

static long long val_to_int(Val v) {
    if (v.type == VT_INT) return v.ival;
    if (v.type == VT_FLT) return (long long)v.fval;
    return 0;
}

static void val_print(Val v) {
    switch (v.type) {
        case VT_INT: printf("%lld\n", v.ival);        break;
        case VT_FLT: printf("%g\n",   v.fval);        break;
        case VT_STR: printf("%s\n",   v.sval);        break;
    }
}

/* ------------------------------------------------------------------ */
/*  Variable table                                                     */
/* ------------------------------------------------------------------ */
typedef struct {
    char  name[64];
    Val   val;
    int   is_const;   /* 1 = varl (immutable) */
    int   used;
} Var;

static Var g_vars[MAX_VARS];
static int g_nvar = 0;

static Var *var_find(const char *name) {
    int i;
    for (i = 0; i < g_nvar; i++)
        if (g_vars[i].used && strcmp(g_vars[i].name, name) == 0)
            return &g_vars[i];
    return NULL;
}

static Var *var_set(const char *name, Val v, int is_const) {
    Var *p = var_find(name);
    if (p) {
        if (p->is_const) {
            fprintf(stderr, "error: cannot modify const '%s'\n", name);
            return NULL;
        }
        p->val = v;
        return p;
    }
    if (g_nvar >= MAX_VARS) { fprintf(stderr, "error: too many variables\n"); return NULL; }
    p = &g_vars[g_nvar++];
    p->used = 1;
    p->is_const = is_const;
    p->val = v;
    strncpy(p->name, name, 63);
    p->name[63] = 0;
    return p;
}

/* ------------------------------------------------------------------ */
/*  Function table                                                     */
/* ------------------------------------------------------------------ */
typedef struct {
    char name[64];
    char params[MAX_PARAMS][64];
    int  nparam;
    int  body_start;   /* line index of first body line */
    int  body_end;     /* line index of end_ */
    int  used;
} Func;

static Func g_funcs[MAX_FUNCS];
static int  g_nfunc = 0;

static Func *func_find(const char *name) {
    int i;
    for (i = 0; i < g_nfunc; i++)
        if (g_funcs[i].used && strcmp(g_funcs[i].name, name) == 0)
            return &g_funcs[i];
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Label table                                                        */
/* ------------------------------------------------------------------ */
typedef struct {
    char name[64];
    int  line;
    int  used;
} Label;

static Label g_labels[MAX_LABELS];
static int   g_nlabel = 0;

static Label *label_find(const char *name) {
    int i;
    for (i = 0; i < g_nlabel; i++)
        if (g_labels[i].used && strcmp(g_labels[i].name, name) == 0)
            return &g_labels[i];
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Source storage                                                     */
/* ------------------------------------------------------------------ */
static char g_lines[MAX_LINES][MAX_LINE_LEN];
static int  g_nlines = 0;

/* ------------------------------------------------------------------ */
/*  Call stack for function returns                                    */
/* ------------------------------------------------------------------ */
typedef struct {
    int return_line;
    /* saved local vars – we just save/restore entire var table */
    Var saved_vars[MAX_VARS];
    int saved_nvar;
} CallFrame;

static CallFrame g_callstack[MAX_CALL_DEPTH];
static int       g_calldepth = 0;
static Val       g_retval;
static int       g_has_retval = 0;

/* ------------------------------------------------------------------ */
/*  Utility: trim in-place, strip comments                            */
/* ------------------------------------------------------------------ */
static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == 0) return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = 0;
    return s;
}

static void strip_comment(char *s) {
    /* remove "; ..." comments, but not inside strings */
    int in_str = 0;
    char *p;
    for (p = s; *p; p++) {
        if (*p == '"') in_str = !in_str;
        if (!in_str && *p == ';') { *p = 0; return; }
    }
}

/* ------------------------------------------------------------------ */
/*  Parse a token as a value: literal or variable lookup               */
/* ------------------------------------------------------------------ */
static Val resolve(const char *tok) {
    if (!tok || !*tok) return val_int(0);

    /* string literal */
    if (tok[0] == '"') {
        size_t len = strlen(tok);
        char buf[256] = {0};
        if (len >= 2 && tok[len-1] == '"') {
            strncpy(buf, tok+1, len-2);
            buf[len-2] = 0;
        } else {
            strncpy(buf, tok+1, 255);
        }
        return val_str(buf);
    }

    /* try variable */
    Var *v = var_find(tok);
    if (v) return v->val;

    /* numeric literal */
    {
        char *endp = NULL;
        /* check for float */
        if (strchr(tok, '.')) {
            double d = strtod(tok, &endp);
            if (endp && *endp == 0) return val_flt(d);
        }
        {
            long long ll = strtoll(tok, &endp, 10);
            if (endp && *endp == 0) return val_int(ll);
        }
    }

    /* fallback: treat as string */
    return val_str(tok);
}

/* ------------------------------------------------------------------ */
/*  Tokenize a rest-of-line into words (respecting quoted strings)     */
/* ------------------------------------------------------------------ */
#define MAX_TOKENS 32

static int tokenize(const char *s, char tokens[][256], int max_tok) {
    int n = 0;
    while (*s && n < max_tok) {
        while (*s && isspace((unsigned char)*s)) s++;
        if (!*s) break;
        if (*s == '"') {
            /* quoted string */
            const char *start = s;
            s++; /* skip opening quote */
            while (*s && *s != '"') s++;
            if (*s == '"') s++; /* skip closing quote */
            {
                int len = (int)(s - start);
                if (len > 255) len = 255;
                strncpy(tokens[n], start, (size_t)len);
                tokens[n][len] = 0;
                n++;
            }
        } else {
            const char *start = s;
            while (*s && !isspace((unsigned char)*s)) s++;
            {
                int len = (int)(s - start);
                if (len > 255) len = 255;
                strncpy(tokens[n], start, (size_t)len);
                tokens[n][len] = 0;
                n++;
            }
        }
    }
    return n;
}

/* ------------------------------------------------------------------ */
/*  Compare two values (for conditions)                                */
/* ------------------------------------------------------------------ */
static int compare_vals(Val a, const char *op, Val b) {
    /* string comparison */
    if (a.type == VT_STR || b.type == VT_STR) {
        int cmp = strcmp(a.sval, b.sval);
        if (strcmp(op, "==") == 0) return cmp == 0;
        if (strcmp(op, "!=") == 0) return cmp != 0;
        if (strcmp(op, "<")  == 0) return cmp <  0;
        if (strcmp(op, ">")  == 0) return cmp >  0;
        if (strcmp(op, "<=") == 0) return cmp <= 0;
        if (strcmp(op, ">=") == 0) return cmp >= 0;
        return 0;
    }
    /* numeric comparison */
    {
        double da = val_to_double(a);
        double db = val_to_double(b);
        if (strcmp(op, "==") == 0) return da == db;
        if (strcmp(op, "!=") == 0) return da != db;
        if (strcmp(op, "<")  == 0) return da <  db;
        if (strcmp(op, ">")  == 0) return da >  db;
        if (strcmp(op, "<=") == 0) return da <= db;
        if (strcmp(op, ">=") == 0) return da >= db;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Find matching end_ for a block starting at line `from`             */
/* ------------------------------------------------------------------ */
static int find_end(int from) {
    int depth = 1;
    int i;
    for (i = from + 1; i < g_nlines; i++) {
        char tmp[MAX_LINE_LEN];
        strncpy(tmp, g_lines[i], MAX_LINE_LEN-1);
        tmp[MAX_LINE_LEN-1] = 0;
        char *line = trim(tmp);
        if (!*line || *line == ';') continue;
        /* check keyword */
        if (strncmp(line, "if__", 4) == 0 ||
            strncmp(line, "loop", 4) == 0 ||
            strncmp(line, "func", 4) == 0)
            depth++;
        else if (strncmp(line, "end_", 4) == 0) {
            depth--;
            if (depth == 0) return i;
        }
    }
    return g_nlines; /* not found */
}

/* ------------------------------------------------------------------ */
/*  Find elif/else/end_ at same depth (for if__ chains)               */
/* ------------------------------------------------------------------ */
static int find_next_branch(int from) {
    int depth = 1;
    int i;
    for (i = from + 1; i < g_nlines; i++) {
        char tmp[MAX_LINE_LEN];
        strncpy(tmp, g_lines[i], MAX_LINE_LEN-1);
        tmp[MAX_LINE_LEN-1] = 0;
        char *line = trim(tmp);
        if (!*line || *line == ';') continue;
        if (strncmp(line, "if__", 4) == 0 ||
            strncmp(line, "loop", 4) == 0 ||
            strncmp(line, "func", 4) == 0)
            depth++;
        else if (strncmp(line, "end_", 4) == 0) {
            depth--;
            if (depth == 0) return i;
        } else if (depth == 1 &&
                   (strncmp(line, "elif", 4) == 0 ||
                    strncmp(line, "else", 4) == 0)) {
            return i;
        }
    }
    return g_nlines;
}

/* ------------------------------------------------------------------ */
/*  Execute range of lines [start, end)  – returns next line to run    */
/*  *jump is set to a goto target if goto was used, or -1              */
/* ------------------------------------------------------------------ */
static int exec_range(int start, int end, int *jump);

/* Execute a single line. Returns the next suggested line index,
   or sets *jump >= 0 for goto / brk_ / retn situations.
   *brk is set to 1 if brk_ was encountered.
   *did_retn is set to 1 if retn was encountered. */
static int exec_line(int idx, int *jump, int *brk, int *did_retn) {
    char buf[MAX_LINE_LEN];
    char kw[8];
    char tokens[MAX_TOKENS][256];
    int  ntok;
    char *rest;

    *jump = -1;
    *brk = 0;
    *did_retn = 0;

    strncpy(buf, g_lines[idx], MAX_LINE_LEN-1);
    buf[MAX_LINE_LEN - 1] = 0;
    strip_comment(buf);
    {
        char *line = trim(buf);
        if (!*line) return idx + 1;

        /* extract 4-byte keyword */
        if (strlen(line) < 4) return idx + 1;
        memcpy(kw, line, 4);
        kw[4] = 0;
        rest = trim(line + 4);
    }

    ntok = tokenize(rest, tokens, MAX_TOKENS);

    /* ---- varm / varl ---- */
    if (strcmp(kw, "varm") == 0 || strcmp(kw, "varl") == 0) {
        if (ntok < 1) { fprintf(stderr, "line %d: %s needs name\n", idx+1, kw); return idx+1; }
        {
            int is_const = (strcmp(kw, "varl") == 0);
            Val v = (ntok >= 2) ? resolve(tokens[1]) : val_int(0);
            var_set(tokens[0], v, is_const);
        }
        return idx + 1;
    }

    /* ---- set_ ---- */
    if (strcmp(kw, "set_") == 0) {
        if (ntok < 2) { fprintf(stderr, "line %d: set_ needs name value\n", idx+1); return idx+1; }
        {
            Val v = resolve(tokens[1]);
            var_set(tokens[0], v, 0);
        }
        return idx + 1;
    }

    /* ---- arithmetic: add_ sub_ mul_ div_ mod_ ---- */
    if (strcmp(kw, "add_") == 0 || strcmp(kw, "sub_") == 0 ||
        strcmp(kw, "mul_") == 0 || strcmp(kw, "div_") == 0 ||
        strcmp(kw, "mod_") == 0) {
        if (ntok < 3) { fprintf(stderr, "line %d: %s needs dest a b\n", idx+1, kw); return idx+1; }
        {
            Val a = resolve(tokens[1]);
            Val b = resolve(tokens[2]);
            Val result;
            /* if either is float, do float arithmetic */
            if (a.type == VT_FLT || b.type == VT_FLT) {
                double da = val_to_double(a);
                double db = val_to_double(b);
                double dr = 0.0;
                if (strcmp(kw, "add_") == 0)      dr = da + db;
                else if (strcmp(kw, "sub_") == 0)  dr = da - db;
                else if (strcmp(kw, "mul_") == 0)  dr = da * db;
                else if (strcmp(kw, "div_") == 0)  { if (db != 0) dr = da / db; }
                else if (strcmp(kw, "mod_") == 0)  { dr = (double)((long long)da % (long long)db); }
                result = val_flt(dr);
            } else {
                long long la = val_to_int(a);
                long long lb = val_to_int(b);
                long long lr = 0;
                if (strcmp(kw, "add_") == 0)      lr = la + lb;
                else if (strcmp(kw, "sub_") == 0)  lr = la - lb;
                else if (strcmp(kw, "mul_") == 0)  lr = la * lb;
                else if (strcmp(kw, "div_") == 0)  { if (lb != 0) lr = la / lb; }
                else if (strcmp(kw, "mod_") == 0)  { if (lb != 0) lr = la % lb; }
                result = val_int(lr);
            }
            /* set or create dest variable (mutable) */
            var_set(tokens[0], result, 0);
        }
        return idx + 1;
    }

    /* ---- prnt ---- */
    if (strcmp(kw, "prnt") == 0) {
        if (ntok < 1) { printf("\n"); return idx+1; }
        {
            Val v = resolve(tokens[0]);
            val_print(v);
        }
        return idx + 1;
    }

    /* ---- inpt ---- */
    if (strcmp(kw, "inpt") == 0) {
        if (ntok < 1) { fprintf(stderr, "line %d: inpt needs varname\n", idx+1); return idx+1; }
        {
            char input_buf[256];
            if (fgets(input_buf, sizeof(input_buf), stdin)) {
                /* strip trailing newline */
                size_t len = strlen(input_buf);
                if (len > 0 && input_buf[len-1] == '\n') input_buf[len-1] = 0;
                var_set(tokens[0], val_str(input_buf), 0);
            }
        }
        return idx + 1;
    }

    /* ---- if__ ---- */
    if (strcmp(kw, "if__") == 0) {
        /* parse condition: tokens[0] OP tokens[2], operator in tokens[1] */
        if (ntok < 3) { fprintf(stderr, "line %d: if__ needs cond\n", idx+1); return idx+1; }
        {
            Val a = resolve(tokens[0]);
            Val b = resolve(tokens[2]);
            int end_line = find_end(idx);

            if (compare_vals(a, tokens[1], b)) {
                /* execute body until next elif/else/end_ */
                int body_end = find_next_branch(idx);
                int jmp = -1;
                exec_range(idx + 1, body_end, &jmp);
                if (jmp >= 0) { *jump = jmp; return end_line + 1; }
            } else {
                /* skip to next elif/else/end_ */
                int next = find_next_branch(idx);
                while (next < end_line) {
                    char tmp2[MAX_LINE_LEN];
                    char *nline;
                    strncpy(tmp2, g_lines[next], MAX_LINE_LEN-1);
                    tmp2[MAX_LINE_LEN-1] = 0;
                    nline = trim(tmp2);

                    if (strncmp(nline, "elif", 4) == 0) {
                        /* parse elif condition */
                        char etokens[MAX_TOKENS][256];
                        int entok = tokenize(trim(nline + 4), etokens, MAX_TOKENS);
                        if (entok >= 3) {
                            Val ea = resolve(etokens[0]);
                            Val eb = resolve(etokens[2]);
                            if (compare_vals(ea, etokens[1], eb)) {
                                int body_end2 = find_next_branch(next);
                                int jmp2 = -1;
                                exec_range(next + 1, body_end2, &jmp2);
                                if (jmp2 >= 0) { *jump = jmp2; return end_line + 1; }
                                return end_line + 1;
                            }
                        }
                        next = find_next_branch(next);
                    } else if (strncmp(nline, "else", 4) == 0) {
                        int jmp3 = -1;
                        exec_range(next + 1, end_line, &jmp3);
                        if (jmp3 >= 0) { *jump = jmp3; return end_line + 1; }
                        return end_line + 1;
                    } else {
                        break;
                    }
                }
            }
            return end_line + 1;
        }
    }

    /* ---- loop ---- */
    if (strcmp(kw, "loop") == 0) {
        if (ntok < 3) { fprintf(stderr, "line %d: loop needs cond\n", idx+1); return idx+1; }
        {
            int end_line = find_end(idx);
            while (1) {
                /* re-resolve each iteration */
                Val a = resolve(tokens[0]);
                Val b = resolve(tokens[2]);
                int jmp = -1;
                if (!compare_vals(a, tokens[1], b)) break;
                exec_range(idx + 1, end_line, &jmp);
                if (jmp == -2) break; /* brk_ */
                if (jmp >= 0) { *jump = jmp; return end_line + 1; }
            }
            return end_line + 1;
        }
    }

    /* ---- brk_ ---- */
    if (strcmp(kw, "brk_") == 0) {
        *brk = 1;
        *jump = -2; /* sentinel for break */
        return idx + 1;
    }

    /* ---- func (definition – skip body during scan) ---- */
    if (strcmp(kw, "func") == 0) {
        /* already registered in pass 1, skip to end_ */
        int end_line = find_end(idx);
        return end_line + 1;
    }

    /* ---- call ---- */
    if (strcmp(kw, "call") == 0) {
        /* call funcname(arg1 arg2 ...) */
        if (ntok < 1) { fprintf(stderr, "line %d: call needs func\n", idx+1); return idx+1; }
        {
            /* re-parse rest to get funcname and args */
            char fname[64] = {0};
            char *paren = strchr(rest, '(');
            if (paren) {
                int flen = (int)(paren - rest);
                if (flen > 63) flen = 63;
                strncpy(fname, rest, (size_t)flen);
                fname[flen] = 0;
                {
                    char *trimmed = trim(fname);
                    memmove(fname, trimmed, strlen(trimmed)+1);
                }
            } else {
                strncpy(fname, tokens[0], 63);
            }
            {
                Func *fn = func_find(fname);
                if (!fn) { fprintf(stderr, "line %d: unknown function '%s'\n", idx+1, fname); return idx+1; }
                /* parse arguments */
                {
                    char arg_tokens[MAX_TOKENS][256];
                    int nargs = 0;
                    if (paren) {
                        char *close = strchr(paren, ')');
                        if (close) {
                            char argbuf[512];
                            int arglen = (int)(close - paren - 1);
                            if (arglen > 0 && arglen < 512) {
                                strncpy(argbuf, paren+1, (size_t)arglen);
                                argbuf[arglen] = 0;
                                nargs = tokenize(trim(argbuf), arg_tokens, MAX_TOKENS);
                            }
                        }
                    }
                    /* save variables, set params, run body */
                    if (g_calldepth >= MAX_CALL_DEPTH) {
                        fprintf(stderr, "line %d: call stack overflow\n", idx+1);
                        return idx+1;
                    }
                    {
                        CallFrame *frame = &g_callstack[g_calldepth++];
                        int pi;
                        int jmp4 = -1;
                        frame->return_line = idx + 1;
                        memcpy(frame->saved_vars, g_vars, sizeof(g_vars));
                        frame->saved_nvar = g_nvar;

                        /* bind parameters */
                        for (pi = 0; pi < fn->nparam && pi < nargs; pi++) {
                            Val av = resolve(arg_tokens[pi]);
                            var_set(fn->params[pi], av, 0);
                        }

                        g_has_retval = 0;
                        exec_range(fn->body_start, fn->body_end, &jmp4);

                        /* restore variables, keeping return value */
                        {
                            Val saved_ret = g_retval;
                            int had_ret = g_has_retval;
                            memcpy(g_vars, frame->saved_vars, sizeof(g_vars));
                            g_nvar = frame->saved_nvar;
                            g_retval = saved_ret;
                            g_has_retval = had_ret;
                        }
                        g_calldepth--;
                    }
                }
            }
        }
        return idx + 1;
    }

    /* ---- retn ---- */
    if (strcmp(kw, "retn") == 0) {
        if (ntok >= 1) {
            g_retval = resolve(tokens[0]);
        } else {
            g_retval = val_int(0);
        }
        g_has_retval = 1;
        *did_retn = 1;
        return idx + 1;
    }

    /* ---- labl (no-op at runtime, registered in pass 1) ---- */
    if (strcmp(kw, "labl") == 0) {
        return idx + 1;
    }

    /* ---- goto ---- */
    if (strcmp(kw, "goto") == 0) {
        if (ntok < 1) { fprintf(stderr, "line %d: goto needs label\n", idx+1); return idx+1; }
        {
            Label *lb = label_find(tokens[0]);
            if (!lb) { fprintf(stderr, "line %d: unknown label '%s'\n", idx+1, tokens[0]); return idx+1; }
            *jump = lb->line;
        }
        return idx + 1;
    }

    /* ---- end_ (consumed by if__/loop/func) ---- */
    if (strcmp(kw, "end_") == 0) {
        return idx + 1;
    }

    /* ---- elif / else (consumed by if__) ---- */
    if (strcmp(kw, "elif") == 0 || strcmp(kw, "else") == 0) {
        return idx + 1;
    }

    fprintf(stderr, "line %d: unknown keyword '%.4s'\n", idx+1, kw);
    return idx + 1;
}

/* ------------------------------------------------------------------ */
/*  Execute a range of lines [start, end)                              */
/* ------------------------------------------------------------------ */
static int exec_range(int start, int end, int *jump) {
    int i = start;
    *jump = -1;
    while (i < end) {
        int jmp = -1, brk_flag = 0, did_retn = 0;
        i = exec_line(i, &jmp, &brk_flag, &did_retn);
        if (did_retn) {
            *jump = -3; /* sentinel: return */
            return i;
        }
        if (jmp == -2) { /* break */
            *jump = -2;
            return i;
        }
        if (jmp >= 0) {
            /* goto */
            if (jmp >= start && jmp < end) {
                i = jmp;
            } else {
                *jump = jmp;
                return i;
            }
        }
    }
    return i;
}

/* ------------------------------------------------------------------ */
/*  Pass 1: register func definitions, labels, also handle varm with  */
/*          func call results like: varm result add(3 4)               */
/* ------------------------------------------------------------------ */
static void pass1_register(void) {
    int i;
    for (i = 0; i < g_nlines; i++) {
        char buf[MAX_LINE_LEN];
        char *line;
        strncpy(buf, g_lines[i], MAX_LINE_LEN-1);
        buf[MAX_LINE_LEN-1] = 0;
        strip_comment(buf);
        line = trim(buf);
        if (!*line) continue;
        if (strlen(line) < 4) continue;

        /* func definition */
        if (strncmp(line, "func", 4) == 0) {
            char *rest2 = trim(line + 4);
            char fname[64] = {0};
            char *paren = strchr(rest2, '(');
            if (paren) {
                int flen = (int)(paren - rest2);
                if (flen > 63) flen = 63;
                strncpy(fname, rest2, (size_t)flen);
                fname[flen] = 0;
                {
                    char *tr = trim(fname);
                    memmove(fname, tr, strlen(tr)+1);
                }
                if (g_nfunc < MAX_FUNCS) {
                    Func *fn = &g_funcs[g_nfunc++];
                    fn->used = 1;
                    strncpy(fn->name, fname, 63);
                    fn->name[63] = 0;
                    fn->nparam = 0;
                    /* parse params */
                    {
                        char *close = strchr(paren, ')');
                        if (close) {
                            char pbuf[256];
                            int plen = (int)(close - paren - 1);
                            if (plen > 0 && plen < 256) {
                                char ptokens[MAX_TOKENS][256];
                                int np, pi;
                                strncpy(pbuf, paren+1, (size_t)plen);
                                pbuf[plen] = 0;
                                np = tokenize(trim(pbuf), ptokens, MAX_PARAMS);
                                for (pi = 0; pi < np && pi < MAX_PARAMS; pi++) {
                                    strncpy(fn->params[pi], ptokens[pi], 63);
                                    fn->params[pi][63] = 0;
                                }
                                fn->nparam = np;
                            }
                        }
                    }
                    fn->body_start = i + 1;
                    fn->body_end = find_end(i);
                }
            }
        }

        /* label */
        if (strncmp(line, "labl", 4) == 0) {
            char *rest2 = trim(line + 4);
            char ltokens[MAX_TOKENS][256];
            int lntok = tokenize(rest2, ltokens, MAX_TOKENS);
            if (lntok >= 1 && g_nlabel < MAX_LABELS) {
                Label *lb = &g_labels[g_nlabel++];
                lb->used = 1;
                strncpy(lb->name, ltokens[0], 63);
                lb->name[63] = 0;
                lb->line = i + 1; /* jump to line after label */
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Load source file                                                   */
/* ------------------------------------------------------------------ */
static int load_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "Cannot open: %s\n", path); return 0; }
    g_nlines = 0;
    while (g_nlines < MAX_LINES &&
           fgets(g_lines[g_nlines], MAX_LINE_LEN, f)) {
        /* remove trailing newline */
        {
            size_t len = strlen(g_lines[g_nlines]);
            if (len > 0 && g_lines[g_nlines][len-1] == '\n')
                g_lines[g_nlines][len-1] = 0;
        }
        g_nlines++;
    }
    fclose(f);
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Handle varm with function call: varm result add(3 4)               */
/*  We detect this pattern and convert it to call + capture retval     */
/* ------------------------------------------------------------------ */
static int handle_varm_call(int idx) {
    char buf[MAX_LINE_LEN];
    char *line, *rest2;
    char tokens2[MAX_TOKENS][256];
    int ntok2;

    strncpy(buf, g_lines[idx], MAX_LINE_LEN-1);
    buf[MAX_LINE_LEN-1] = 0;
    strip_comment(buf);
    line = trim(buf);

    if (strncmp(line, "varm", 4) != 0) return 0;
    rest2 = trim(line + 4);
    ntok2 = tokenize(rest2, tokens2, MAX_TOKENS);
    if (ntok2 < 2) return 0;

    /* check if second token contains '(' – means function call */
    if (!strchr(tokens2[1], '(')) return 0;

    /* it's a varm with function call: varm varname funcname(args) */
    {
        char varname[64];
        char call_rest[512];
        char *second_start;
        int jmp = -1, brk = 0, did_retn = 0;
        /* build a temporary "call funcname(args)" line */
        strncpy(varname, tokens2[0], 63);
        varname[63] = 0;

        second_start = strstr(rest2, tokens2[1]);
        if (!second_start) return 0;

        snprintf(call_rest, sizeof(call_rest), "call %s", second_start);

        /* temporarily replace the line */
        {
            char saved[MAX_LINE_LEN];
            strncpy(saved, g_lines[idx], MAX_LINE_LEN);
            strncpy(g_lines[idx], call_rest, MAX_LINE_LEN-1);
            g_lines[idx][MAX_LINE_LEN-1] = 0;

            exec_line(idx, &jmp, &brk, &did_retn);

            /* restore line */
            strncpy(g_lines[idx], saved, MAX_LINE_LEN);
        }

        /* capture return value into variable */
        if (g_has_retval) {
            var_set(varname, g_retval, 0);
            g_has_retval = 0;
        } else {
            var_set(varname, val_int(0), 0);
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Main: enhanced exec_range that handles varm+call pattern           */
/* ------------------------------------------------------------------ */
static int exec_range_enhanced(int start, int end, int *jump) {
    int i = start;
    *jump = -1;
    while (i < end) {
        /* check for varm with function call pattern */
        if (handle_varm_call(i)) {
            i++;
            continue;
        }
        {
            int jmp = -1, brk_flag = 0, did_retn = 0;
            i = exec_line(i, &jmp, &brk_flag, &did_retn);
            if (did_retn) { *jump = -3; return i; }
            if (jmp == -2) { *jump = -2; return i; }
            if (jmp >= 0) {
                if (jmp >= start && jmp < end) {
                    i = jmp;
                } else {
                    *jump = jmp;
                    return i;
                }
            }
        }
    }
    return i;
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[]) {
    int jmp = -1;
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <script.spl>\n", argv[0]);
        return 1;
    }
    if (!load_file(argv[1])) return 1;

    pass1_register();

    exec_range_enhanced(0, g_nlines, &jmp);

    return 0;
}
