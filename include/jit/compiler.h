#ifndef __JIT_COMPILER_H
#define __JIT_COMPILER_H

#include <jit/compilation-unit.h>
#include <jit/basic-block.h>
#include <vm/buffer.h>
#include <vm/stack.h>
#include <vm/vm.h>

struct buffer;
struct compilation_unit;

struct jit_trampoline {
	struct buffer *objcode;
};

struct parse_context {
	struct compilation_unit *cu;
	struct basic_block *bb;

	unsigned char *insn_start;
	unsigned char *code;
	unsigned long offset;
	unsigned long code_size;
	unsigned char opc;
};

int analyze_control_flow(struct compilation_unit *);
int convert_to_ir(struct compilation_unit *);
int analyze_liveness(struct compilation_unit *);
int select_instructions(struct compilation_unit *cu);
int allocate_registers(struct compilation_unit *cu);
int emit_machine_code(struct compilation_unit *);
void *jit_magic_trampoline(struct compilation_unit *);

struct jit_trampoline *alloc_jit_trampoline(void);
struct jit_trampoline *build_jit_trampoline(struct compilation_unit *);
void free_jit_trampoline(struct jit_trampoline *);

void *jit_prepare_method(struct methodblock *);

static inline void *trampoline_ptr(struct methodblock *method)
{
	return buffer_ptr(method->trampoline->objcode);
}

extern bool opt_trace_method;
extern bool opt_trace_cfg;
extern bool opt_trace_tree_ir;
extern bool opt_trace_liveness;
extern bool opt_trace_regalloc;
extern bool opt_trace_machine_code;

void trace_method(struct compilation_unit *);
void trace_cfg(struct compilation_unit *);
void trace_tree_ir(struct compilation_unit *);
void trace_liveness(struct compilation_unit *);
void trace_regalloc(struct compilation_unit *);
void trace_machine_code(struct compilation_unit *);

#endif