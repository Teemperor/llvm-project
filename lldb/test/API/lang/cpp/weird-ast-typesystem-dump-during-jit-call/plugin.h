#ifndef PLUGIN_H_IN
#define PLUGIN_H_IN

struct Widget;

extern "C" {
void plugin_entry(void);

// Spins on a condition variable (via sleep_for) for a few seconds when
// called. This is meant to be called from LLDB's expression evaluator
// ('expression -- SlowCompute()') so that the JIT'd function call blocks
// the calling thread -- and, more importantly, keeps LLDB's private state
// thread busy running the inferior and waiting for it to hit the
// function-call's temporary return breakpoint -- for long enough that a
// second thread has a comfortable window to poke at LLDB's internal
// TypeSystemClang/ASTImporter state concurrently.
int SlowCompute(void);

// Forces the dylib's 'Widget' type (see plugin.cpp) to be materialized: it
// returns a pointer that the test dereferences via
// 'expression *plugin_make_widget()', which makes
// DWARFASTParserClang/TypeSystemClang actually parse and complete
// 'Widget' into the per-module (and, from there, the per-target scratch)
// Clang AST.
Widget *plugin_make_widget(void);
}

#endif // PLUGIN_H_IN
