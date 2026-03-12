/*
 * disasm_stub.c — Stub implementations of libdisasm symbols.
 *
 * libbreakpad.a includes disassembler_x86.o which references libdisasm.
 * On non-x86 targets (e.g. ARM32/ARM64) libdisasm is not available, and
 * the x86 exploitability engine is never executed at runtime.
 *
 * These stubs satisfy the linker without pulling in a foreign-arch library.
 * They are unreachable because MinidumpProcessor is constructed without
 * exploitability analysis (enable_exploitability = false).
 *
 * IMPORTANT: all symbols must have C linkage so they match the unmangled
 * references in disassembler_x86.o (libdis.h wraps them in extern "C").
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque placeholders — only pointers are passed through the stubs. */
typedef struct { int _unused; } x86_insn_t_stub;
typedef struct { int _unused; } x86_op_t_stub;
typedef int x86_options_stub;
typedef void (*DISASM_REPORTER_stub)(int, const char *, void *);

int  x86_init(x86_options_stub options, DISASM_REPORTER_stub reporter,
              void *arg)                                    { return 0; }
int  x86_cleanup(void)                                     { return 0; }
void x86_oplist_free(x86_insn_t_stub *insn)                { (void)insn; }

unsigned int x86_disasm(unsigned char *buf, unsigned int buf_len,
                        unsigned int buf_rva, unsigned int offset,
                        x86_insn_t_stub *insn)
{ (void)buf; (void)buf_len; (void)buf_rva; (void)offset; (void)insn; return 0; }

int            x86_insn_is_valid(x86_insn_t_stub *insn) { (void)insn; return 0; }
x86_op_t_stub *x86_operand_1st(x86_insn_t_stub *insn)  { (void)insn; return 0; }
x86_op_t_stub *x86_operand_2nd(x86_insn_t_stub *insn)  { (void)insn; return 0; }

#ifdef __cplusplus
} /* extern "C" */
#endif
