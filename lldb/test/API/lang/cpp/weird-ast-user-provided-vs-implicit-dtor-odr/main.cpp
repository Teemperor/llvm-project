#include "plugin.h"

// This is the exe's definition of 'Logger': it has a user-provided,
// out-of-line, non-trivial destructor. The dylib below (see plugin.cpp)
// declares an identical-looking 'Logger' with the exact same field layout,
// but with NO user-declared destructor at all, so it gets an
// implicit *trivial* destructor instead. This is a deliberate ODR
// violation: two CXXRecordDecls named 'Logger' that only disagree on
// whether the destructor is user-provided/non-trivial or
// implicit/trivial. Clang caches "hasTrivialDestructor()" (and related
// bits) once, in the class's DefinitionData, at completion time
// (CXXRecordDecl::completeDefinition()). LLDB's DWARFASTParserClang /
// TypeSystemClang machinery instead fabricates these definitions post-hoc
// from two independent DWARF definitions and can end up trying to
// complete/attach an implicit destructor to the very same (merged)
// canonical RecordDecl that already has a user-provided, non-trivial,
// out-of-line destructor attached from the other module.
struct Logger {
  ~Logger() { id = 0; /* user-provided, non-trivial */ }
  int id;
};

Logger global_logger{1};

int main() {
  global_logger.id = 2;

  plugin_init();
  plugin_entry();
  return 0;
}
