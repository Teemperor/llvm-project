//===- LLDBTableGenBackends.h - Declarations for LLDB TableGen Backends ---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations for all of the LLDB TableGen
// backends. A "TableGen backend" is just a function. See
// "$LLVM_ROOT/utils/TableGen/TableGenBackends.h" for more info.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LLDB_UTILS_TABLEGEN_TABLEGENBACKENDS_H
#define LLVM_LLDB_UTILS_TABLEGEN_TABLEGENBACKENDS_H

#include <string>

namespace llvm {
  class raw_ostream;
  class RecordKeeper;
}

using llvm::raw_ostream;
using llvm::RecordKeeper;

namespace lldb_private {

void EmitOptionDefs(RecordKeeper &RK, raw_ostream &OS);

} // end namespace clang

#endif
