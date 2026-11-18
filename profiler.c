/*
 * greenServeFE VM profiler implementation.
 *
 * This file collects execution statistics while the VM is running and writes
 * a human-readable report when profiling finishes. The profiler is deliberately
 * kept separate from the VM's execution logic: callers report events through
 * the gsprof_* functions declared by profiler.h.
 */

#define _POSIX_C_SOURCE 200809L

#include "profiler.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Fixed capacities keep profiler storage predictable and avoid runtime growth. */
#define GSPROF_LINE_CAP 2048
#define GSPROF_OP_CAP 256
#define GSPROF_FUNCTION_CAP 512
#define GSPROF_ACTIVE_CAP 2048

/*
 * Aggregated statistics for one source location.
 * code_name distinguishes files/code objects that use the same line number.
 */
typedef struct {
    char code_name[160];
    int line;
    uint64_t hits;
} GSProfLine;

/* Aggregated call count and inclusive execution time for one function name. */
typedef struct {
    char name[160];
    uint64_t calls;
    double inclusive_seconds;
} GSProfFunction;

/*
 * One active function invocation.
 * The index points into GSProfiler.functions; started marks its entry time.
 */
typedef struct {
    size_t function_index;
    struct timespec started;
} GSProfActive;

/*
 * Complete profiler state.
 *
 * The arrays store opcode counts, hot source locations, known functions, and
 * the active function-call stack. The remaining fields hold overall execution
 * counters, peak VM depths, timing boundaries, and the enabled/disabled flag.
 */
typedef struct {
    uint64_t op_hits[GSPROF_OP_CAP];

    GSProfLine lines[GSPROF_LINE_CAP];
    size_t line_count;

    GSProfFunction functions[GSPROF_FUNCTION_CAP];
    size_t function_count;

    GSProfActive active_stack[GSPROF_ACTIVE_CAP];
    size_t active_count;

    uint64_t instructions;
    uint64_t calls;

    size_t peak_stack;
    size_t peak_frames;

    struct timespec started;
    struct timespec ended;

    int active;
} GSProfiler;

/* The implementation maintains one process-wide profiler instance. */
static GSProfiler prof;

/*
 * Convert two monotonic-clock timestamps into a duration in seconds.
 * Separating seconds and nanoseconds preserves sub-second precision.
 */
static double elapsed_seconds(struct timespec a, struct timespec b)
{
    return (double)(b.tv_sec - a.tv_sec) +
           (double)(b.tv_nsec - a.tv_nsec) / 1000000000.0;
}

/*
 * Ensure report labels are printable even when the caller supplies NULL or
 * an empty string. This does not modify the caller's original string.
 */
static const char *safe_name(const char *s)
{
    return (s && *s) ? s : "<unknown>";
}

/*
 * Reset all collected state.
 * memset also clears counters, array lengths, timestamps, and active status.
 */
void gsprof_reset(void)
{
    memset(&prof, 0, sizeof prof);
}

/*
 * Enable profiling and capture the overall start time.
 * CLOCK_MONOTONIC is used so wall-clock adjustments do not affect durations.
 */
void gsprof_start(void)
{
    prof.active = 1;
    (void)clock_gettime(CLOCK_MONOTONIC, &prof.started);
}

/*
 * Record one executed bytecode instruction.
 *
 * In addition to the total instruction count, this updates the opcode
 * histogram and peak VM stack/frame depths. When a valid source line is
 * available, its hit count is incremented or a new location entry is added.
 */
void gsprof_record_instruction(const GSCode *code, GSOp op, int line,
                               size_t stack_depth, size_t frame_depth)
{
    /* Ignore events unless profiling has been started. */
    if (!prof.active) return;

    /* Count every instruction observed by the VM. */
    prof.instructions++;

    /* Count this opcode only when its numeric value fits the fixed table. */
    if ((unsigned)op < GSPROF_OP_CAP) {
        prof.op_hits[(unsigned)op]++;
    }

    /* Keep the largest operand-stack and call-frame depths seen so far. */
    if (stack_depth > prof.peak_stack) {
        prof.peak_stack = stack_depth;
    }

    if (frame_depth > prof.peak_frames) {
        prof.peak_frames = frame_depth;
    }

    /* A non-positive line number cannot identify a useful source location. */
    if (line <= 0) return;

    /* Use a safe code-object name so comparisons and report output are valid. */
    const char *name = safe_name(code ? code->name : NULL);

    /* Search existing entries to aggregate hits for the same file and line. */
    for (size_t i = 0; i < prof.line_count; i++) {
        GSProfLine *entry = &prof.lines[i];

        if (entry->line == line && strcmp(entry->code_name, name) == 0) {
            entry->hits++;
            return;
        }
    }

    /* Stop recording new locations when the fixed table is full. */
    if (prof.line_count >= GSPROF_LINE_CAP) return;

    /* Create a new source-location entry and initialize its first hit. */
    GSProfLine *entry = &prof.lines[prof.line_count++];

    (void)snprintf(entry->code_name, sizeof entry->code_name, "%s", name);
    entry->line = line;
    entry->hits = 1;
}

/*
 * Record a VM CALL opcode.
 * This counter is distinct from function-entry counts and tracks CALL events.
 */
void gsprof_record_call(void)
{
    if (prof.active) {
        prof.calls++;
    }
}

/*
 * Mark entry into a function.
 *
 * The function is looked up by name so repeated invocations share one
 * aggregate record. A separate active-stack entry is pushed for each
 * invocation to measure its inclusive elapsed time when it returns.
 */
void gsprof_enter_function(const char *name)
{
    /* Do not collect events when disabled or when the active stack is full. */
    if (!prof.active || prof.active_count >= GSPROF_ACTIVE_CAP) return;

    const char *label = safe_name(name);
    size_t index = 0;

    /* Find an existing aggregate record with this function name. */
    while (index < prof.function_count &&
           strcmp(prof.functions[index].name, label) != 0) {
        index++;
    }

    /* Create a record if this function name has not been seen before. */
    if (index == prof.function_count) {
        /* Do not exceed the fixed function table capacity. */
        if (prof.function_count >= GSPROF_FUNCTION_CAP) return;

        GSProfFunction *fn = &prof.functions[prof.function_count++];

        (void)snprintf(fn->name, sizeof fn->name, "%s", label);
        fn->calls = 0;
        fn->inclusive_seconds = 0.0;
    }

    /* Count this invocation in the aggregate function statistics. */
    prof.functions[index].calls++;

    /* Push this invocation and remember when its execution began. */
    GSProfActive *active = &prof.active_stack[prof.active_count++];

    active->function_index = index;
    (void)clock_gettime(CLOCK_MONOTONIC, &active->started);
}

/*
 * Mark function exit and add its elapsed duration to the aggregate record.
 * The active stack is LIFO, matching nested function-call execution.
 */
void gsprof_leave_function(void)
{
    /* Ignore unmatched exits and events received while profiling is disabled. */
    if (!prof.active || !prof.active_count) return;

    struct timespec ended;

    /* Capture the exit time before removing the active invocation. */
    (void)clock_gettime(CLOCK_MONOTONIC, &ended);

    /* Pop the most recently entered function invocation. */
    GSProfActive active = prof.active_stack[--prof.active_count];

    /* Add inclusive time, which includes time spent in child calls. */
    prof.functions[active.function_index].inclusive_seconds +=
        elapsed_seconds(active.started, ended);
}

/*
 * qsort comparator for function records.
 * Sort by descending inclusive time; use the name as a stable tie-breaker.
 */
static int function_order(const void *a, const void *b)
{
    const GSProfFunction *x = (const GSProfFunction *)a;
    const GSProfFunction *y = (const GSProfFunction *)b;

    if (x->inclusive_seconds < y->inclusive_seconds) return 1;
    if (x->inclusive_seconds > y->inclusive_seconds) return -1;

    return strcmp(x->name, y->name);
}

/*
 * qsort comparator for source locations.
 * Sort by descending hit count, then code-object name, then line number.
 */
static int line_order(const void *a, const void *b)
{
    const GSProfLine *x = (const GSProfLine *)a;
    const GSProfLine *y = (const GSProfLine *)b;

    if (x->hits < y->hits) return 1;
    if (x->hits > y->hits) return -1;

    int by_name = strcmp(x->code_name, y->code_name);
    if (by_name) return by_name;

    return (x->line > y->line) - (x->line < y->line);
}

/*
 * Stop profiling and write the complete report to the supplied FILE stream.
 *
 * The report contains overall timing and VM counters, the most expensive
 * functions, opcode frequencies, and the hottest source locations. The
 * function and line arrays are sorted in place to present their leaders first.
 */
void gsprof_finish(FILE *out, const char *source_path, int exit_code)
{
    /* A report requires both an active profiling session and a valid stream. */
    if (!prof.active || !out) return;

    /* Capture the end timestamp, then disable further event collection. */
    (void)clock_gettime(CLOCK_MONOTONIC, &prof.ended);
    prof.active = 0;

    /* Calculate total elapsed time and instruction throughput safely. */
    double seconds = elapsed_seconds(prof.started, prof.ended);
    double rate = seconds > 0.0 ? (double)prof.instructions / seconds : 0.0;

    /* Print report heading and execution identity/status. */
    fprintf(out, "\n=== greenServeFE VM profiler ===\n");
    fprintf(out, "Source: %s\n", safe_name(source_path));
    fprintf(out, "Exit status: %d\n", exit_code);

    /* Print overall runtime and VM execution counters. */
    fprintf(out, "Elapsed: %.6f s\n", seconds);
    fprintf(out, "Executed bytecode instructions: %" PRIu64 "\n",
            prof.instructions);
    fprintf(out, "Instruction throughput: %.0f instructions/s\n", rate);
    fprintf(out, "CALL opcodes: %" PRIu64 "\n", prof.calls);
    fprintf(out, "Peak operand stack depth: %zu\n", prof.peak_stack);
    fprintf(out, "Peak VM frame depth: %zu\n", prof.peak_frames);

    /*
     * Sort function aggregates by inclusive execution time and print at most
     * twenty entries so the report remains concise for large programs.
     */
    qsort(prof.functions, prof.function_count, sizeof prof.functions[0],
          function_order);

    fprintf(out, "\nFunction timing (inclusive; includes child calls):\n");

    if (!prof.function_count) {
        fprintf(out, "  (no VM frames recorded)\n");
    } else {
        size_t fn_limit = prof.function_count < 20 ? prof.function_count : 20;

        for (size_t i = 0; i < fn_limit; i++) {
            fprintf(out, "  %-32s calls=%-8" PRIu64 " total=%0.6f s\n",
                    prof.functions[i].name, prof.functions[i].calls,
                    prof.functions[i].inclusive_seconds);
        }
    }

    /*
     * Print opcode frequencies in opcode-number order. op_name() supplies
     * the readable name; unknown values receive a safe fallback label.
     */
    fprintf(out, "\nOpcode counts:\n");

    int any = 0;

    for (unsigned op = 0; op < GSPROF_OP_CAP; op++) {
        if (!prof.op_hits[op]) continue;

        const char *name = op_name((GSOp)op);

        fprintf(out, "  %-20s %" PRIu64 "\n",
                name ? name : "<unknown>", prof.op_hits[op]);

        any = 1;
    }

    /* Explain an empty opcode table instead of leaving the section blank. */
    if (!any) {
        fprintf(out, "  (no instructions executed)\n");
    }

    /*
     * Sort source locations by hit count and print the twenty hottest lines.
     * Each entry identifies both the code object and its source line.
     */
    qsort(prof.lines, prof.line_count, sizeof prof.lines[0], line_order);

    fprintf(out, "\nHot source locations (most VM instructions first):\n");

    if (!prof.line_count) {
        fprintf(out, "  (no source-line instructions recorded)\n");
    } else {
        size_t limit = prof.line_count < 20 ? prof.line_count : 20;

        for (size_t i = 0; i < limit; i++) {
            fprintf(out, "  %-32s line %-5d %" PRIu64 " instructions\n",
                    prof.lines[i].code_name, prof.lines[i].line,
                    prof.lines[i].hits);
        }
    }

    /* Close the report with a matching footer. */
    fprintf(out, "=== end profiler report ===\n");
}
