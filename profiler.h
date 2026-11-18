#ifndef GSFE_PROFILER_H
#define GSFE_PROFILER_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "bytecode.h"

/*
 * Lightweight, opt-in greenServeFE VM profiler.
 * The profiler observes real VM dispatches; it does not alter bytecode or
 * the language/native-module ABI.
 */
void gsprof_reset(void);
void gsprof_start(void);
void gsprof_record_instruction(const GSCode *code, GSOp op, int line,
                               size_t stack_depth, size_t frame_depth);
void gsprof_record_call(void);
void gsprof_enter_function(const char *name);
void gsprof_leave_function(void);
void gsprof_finish(FILE *out, const char *source_path, int exit_code);

#endif
