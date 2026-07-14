//===-- TypeSystemCpp.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TypeSystemCpp.h"

#include "Plugins/SymbolFile/DWARF/DWARFASTParserCpp.h"

#include "Plugins/ExpressionParser/Clang/ClangFunctionCaller.h"
#include "Plugins/ExpressionParser/Clang/ClangPersistentVariables.h"
#include "Plugins/ExpressionParser/Clang/ClangUserExpression.h"
#include "Plugins/ExpressionParser/Clang/ClangUtilityFunction.h"

#include "lldb/Core/DumpDataExtractor.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Host/StreamFile.h"
#include "lldb/Expression/UtilityFunction.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Target/Language.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/Stream.h"
#include "lldb/ValueObject/ValueObject.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/bit.h"
#include "llvm/Support/MathExtras.h"

using namespace lldb_private;
using namespace lldb;

using cpp_typesystem::Field;

/// Peel typedef/cv-qualifier "sugar" off a type to reach its canonical type.
/// Mirrors clang's RemoveWrappingTypes: queries about layout and children
/// should see through aliases and qualifiers.
static cpp_typesystem::Type *Desugar(cpp_typesystem::Type *t) {
  while (auto *sugar = llvm::dyn_cast_or_null<cpp_typesystem::SugarType>(t))
    t = sugar->GetUnderlyingType();
  return t;
}

// Append the namespace qualification for `ns` (outermost first), skipping
// inline namespaces so that e.g. `std::__1` prints as `std::`.
static void AppendNamespacePrefix(const cpp_typesystem::Namespace *ns,
                                  std::string &out) {
  if (!ns)
    return;
  AppendNamespacePrefix(ns->GetParent(), out);
  if (!ns->IsInline()) {
    out += ns->GetName().GetName().str();
    out += "::";
  }
}

static std::string BuildDisplayName(cpp_typesystem::Type *t);

// Render a function signature in C declarator form, placing `decl` (e.g. "" for
// a plain function, "(*)" for a function pointer, "(&)" for a reference) between
// the return type and the parameter list: `int (*)(const char *)`.
static std::string BuildFunctionName(cpp_typesystem::FunctionType *fn,
                                     llvm::StringRef decl) {
  std::string ret = fn->GetReturnType() ? BuildDisplayName(fn->GetReturnType())
                                        : std::string("void");
  std::string params;
  for (uint32_t i = 0, e = fn->GetNumParameters(); i != e; ++i) {
    if (!params.empty())
      params += ", ";
    params += BuildDisplayName(fn->GetParameterAtIndex(i));
  }
  if (fn->IsVariadic())
    params += params.empty() ? "..." : ", ...";
  else if (params.empty())
    params = "void";
  return ret + " " + decl.str() + "(" + params + ")";
}

// Render a single template argument for the display name.
static std::string
BuildTemplateArgName(const cpp_typesystem::TemplateArgument &arg) {
  if (arg.kind == lldb::eTemplateArgumentKindType)
    return arg.type.Get() ? BuildDisplayName(arg.type.Get())
                          : std::string("void");
  // Integral (non-type) argument: print its value, honoring signedness.
  cpp_typesystem::Type *value_type = arg.type.Get();
  bool is_signed =
      !value_type || value_type->GetEncoding() == lldb::eEncodingSint;
  if (is_signed)
    return std::to_string(static_cast<int64_t>(arg.integral_value));
  return std::to_string(arg.integral_value);
}

// Build a type's (simplified) display name from the type model: qualified with
// its declaring namespaces (inline ones skipped) and, for a class-template
// instantiation, its template arguments with defaulted ones hidden.
static std::string BuildDisplayName(cpp_typesystem::Type *t) {
  using namespace cpp_typesystem;
  if (!t)
    return "";

  // Composite types have no name of their own; build them from their parts.
  if (llvm::isa<ArrayType>(t)) {
    // For a multidimensional C array the DWARF/type nesting goes
    // outermost-dimension first (`T[2][3]` == array-of-2 of array-of-3 of T),
    // but the element name must be printed innermost-last. Peel the whole
    // array chain collecting dimensions left-to-right, then append them after
    // the innermost element's name so the brackets keep their source order.
    std::string dims;
    cpp_typesystem::Type *cur = t;
    while (auto *array = llvm::dyn_cast<ArrayType>(cur)) {
      if (std::optional<uint64_t> n = array->GetNumElements())
        dims += llvm::formatv("[{0}]", *n).str();
      else
        dims += "[]";
      cur = array->GetElementType();
    }
    return BuildDisplayName(cur) + dims;
  }
  if (auto *ptr = llvm::dyn_cast<PointerType>(t)) {
    cpp_typesystem::Type *pointee = ptr->GetPointeeType();
    if (auto *fn = llvm::dyn_cast_or_null<FunctionType>(pointee))
      return BuildFunctionName(fn, "(*)");
    return (pointee ? BuildDisplayName(pointee) : std::string("void")) + " *";
  }
  if (auto *ref = llvm::dyn_cast<ReferenceType>(t)) {
    cpp_typesystem::Type *pointee = ref->GetPointeeType();
    if (auto *fn = llvm::dyn_cast_or_null<FunctionType>(pointee))
      return BuildFunctionName(fn, ref->IsRValue() ? "(&&)" : "(&)");
    return (pointee ? BuildDisplayName(pointee) : std::string("void")) +
           (ref->IsRValue() ? " &&" : " &");
  }
  if (auto *fn = llvm::dyn_cast<FunctionType>(t))
    return BuildFunctionName(fn, "");
  if (auto *cv = llvm::dyn_cast<CVQualifiedType>(t)) {
    std::string result;
    if (cv->IsConst())
      result += "const ";
    if (cv->IsVolatile())
      result += "volatile ";
    return result + (cv->GetUnderlyingType()
                         ? BuildDisplayName(cv->GetUnderlyingType())
                         : "");
  }

  // Named leaf type (record/typedef/enum/builtin). Builtins carry no
  // unqualified name; fall back to their stored name.
  llvm::StringRef unqualified = t->GetUnqualifiedName().GetName();
  if (unqualified.empty())
    return t->GetName().GetName().str();

  std::string result;
  AppendNamespacePrefix(t->GetDeclContext(), result);

  // For a class-template instantiation we have modeled args, so reconstruct
  // "base<non-default args>". Otherwise use the unqualified spelling verbatim
  // (this also covers not-yet-completed templates, whose args aren't parsed).
  auto *rec = llvm::dyn_cast<RecordType>(t);
  if (rec && rec->GetNumTemplateArguments() > 0) {
    result += unqualified.substr(0, unqualified.find('<')).str();
    std::string args;
    for (uint32_t i = 0; i < rec->GetNumTemplateArguments(); ++i) {
      const TemplateArgument *arg = rec->GetTemplateArgumentAtIndex(i);
      if (arg->is_default)
        continue;
      if (!args.empty())
        args += ", ";
      args += BuildTemplateArgName(*arg);
    }
    if (!args.empty())
      result += "<" + args + ">";
  } else {
    result += unqualified.str();
  }
  return result;
}


LLDB_PLUGIN_DEFINE(TypeSystemCpp)

char TypeSystemCpp::ID;
char ScratchTypeSystemCpp::ID;

TypeSystemCpp::TypeSystemCpp(llvm::StringRef name, llvm::Triple triple)
    : m_display_name(name.str()), m_triple(std::move(triple)),
      m_context(cpp_typesystem::LanguageOpts(m_triple)) {}

TypeSystemCpp::~TypeSystemCpp() = default;

plugin::dwarf::DWARFASTParser *TypeSystemCpp::GetDWARFParser() {
  if (!m_dwarf_ast_parser_up)
    m_dwarf_ast_parser_up = std::make_unique<DWARFASTParserCpp>(*this);
  return m_dwarf_ast_parser_up.get();
}

CompilerType TypeSystemCpp::GetCompilerType(cpp_typesystem::Type *type) {
  return CompilerType(weak_from_this(), type);
}

lldb::TypeSystemSP TypeSystemCpp::Create(llvm::StringRef name,
                                         llvm::Triple triple) {
  return std::make_shared<TypeSystemCpp>(name, std::move(triple));
}

TypeSystemSP TypeSystemCpp::CreateInstance(LanguageType language,
                                           Module *module, Target *target) {
  return TypeSystemSP();
}

LanguageSet TypeSystemCpp::GetSupportedLanguagesForTypes() {
  return LanguageSet();
}

LanguageSet TypeSystemCpp::GetSupportedLanguagesForExpressions() {
  return LanguageSet();
}

void TypeSystemCpp::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                "C/C++/Objective-C++ TypeSystem plug-in",
                                CreateInstance, GetSupportedLanguagesForTypes(),
                                GetSupportedLanguagesForExpressions());
}

void TypeSystemCpp::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

ScratchTypeSystemCpp::ScratchTypeSystemCpp(Target &target, llvm::Triple triple)
    : TypeSystemCpp(std::string("scratch TypeSystemCpp for ") +
                        target.GetArchitecture().GetArchitectureName(),
                    std::move(triple)),
      m_target_wp(target.shared_from_this()) {}

UserExpression *ScratchTypeSystemCpp::GetUserExpression(
    llvm::StringRef expr, llvm::StringRef prefix, SourceLanguage language,
    Expression::ResultType desired_type,
    const EvaluateExpressionOptions &options, ValueObject *ctx_obj) {
  TargetSP target = m_target_wp.lock();
  if (!target)
    return nullptr;
  return new ClangUserExpression(*target, expr, prefix, language, desired_type,
                                 options, ctx_obj);
}

FunctionCaller *ScratchTypeSystemCpp::GetFunctionCaller(
    const CompilerType &return_type, const Address &function_address,
    const ValueList &arg_value_list, const char *name) {
  TargetSP target = m_target_wp.lock();
  if (!target)
    return nullptr;
  Process *process = target->GetProcessSP().get();
  if (!process)
    return nullptr;
  return new ClangFunctionCaller(*process, return_type, function_address,
                                 arg_value_list, name);
}

std::unique_ptr<UtilityFunction>
ScratchTypeSystemCpp::CreateUtilityFunction(std::string text,
                                            std::string name) {
  TargetSP target = m_target_wp.lock();
  if (!target)
    return {};
  return std::make_unique<ClangUtilityFunction>(
      *target, std::move(text), std::move(name),
      target->GetDebugUtilityExpression());
}

PersistentExpressionState *
ScratchTypeSystemCpp::GetPersistentExpressionState() {
  if (!m_persistent_variables) {
    TargetSP target = m_target_wp.lock();
    if (!target)
      return nullptr;
    m_persistent_variables =
        std::make_unique<ClangPersistentVariables>(target->shared_from_this());
  }
  return m_persistent_variables.get();
}

ConstString TypeSystemCpp::DeclGetName(void *opaque_decl) {
  return ConstString();
}

CompilerType TypeSystemCpp::GetTypeForDecl(void *opaque_decl) {
  return CompilerType();
}

ConstString TypeSystemCpp::DeclContextGetName(void *opaque_decl_ctx) {
  return ConstString();
}

ConstString
TypeSystemCpp::DeclContextGetScopeQualifiedName(void *opaque_decl_ctx) {
  return ConstString();
}

bool TypeSystemCpp::DeclContextIsClassMethod(void *opaque_decl_ctx) {
  return false;
}

bool TypeSystemCpp::DeclContextIsContainedInLookup(
    void *opaque_decl_ctx, void *other_opaque_decl_ctx) {
  return false;
}

LanguageType TypeSystemCpp::DeclContextGetLanguage(void *opaque_decl_ctx) {
  return eLanguageTypeUnknown;
}

#ifndef NDEBUG
bool TypeSystemCpp::Verify(opaque_compiler_type_t type) { return true; }
#endif

bool TypeSystemCpp::IsArrayType(opaque_compiler_type_t type,
                                CompilerType *element_type, uint64_t *size,
                                bool *is_incomplete) {
  if (element_type)
    element_type->Clear();
  if (size)
    *size = 0;
  if (is_incomplete)
    *is_incomplete = false;

  auto *array = llvm::dyn_cast_or_null<cpp_typesystem::ArrayType>(
      type ? Desugar(GetCppType(type)) : nullptr);
  if (!array)
    return false;

  if (element_type)
    *element_type = GetCompilerType(array->GetElementType());
  if (std::optional<uint64_t> num_elements = array->GetNumElements()) {
    if (size)
      *size = *num_elements;
  } else if (is_incomplete) {
    *is_incomplete = true;
  }
  return true;
}

bool TypeSystemCpp::IsAggregateType(opaque_compiler_type_t type) {
  return type && GetCppType(type)->IsAggregate();
}

bool TypeSystemCpp::IsCharType(opaque_compiler_type_t type) {
  if (!type)
    return false;
  // Strip typedef/cv sugar so `const char` (e.g. the pointee of `const char *`)
  // is recognized. Match clang's narrow-char set: char/signed char/unsigned
  // char (not wchar_t). Identify them by their canonical display format and
  // size rather than pointer identity, so this also works for a type reached
  // through another Context (e.g. an expression result mapped into the scratch
  // TypeSystemCpp).
  cpp_typesystem::Type *t = Desugar(GetCppType(type));
  auto *builtin = llvm::dyn_cast<cpp_typesystem::BuiltinType>(t);
  return builtin && builtin->GetFormat() == lldb::eFormatChar &&
         builtin->GetByteSize().value_or(0) == 1;
}

bool TypeSystemCpp::IsCompleteType(opaque_compiler_type_t type) {
  return false;
}

bool TypeSystemCpp::IsDefined(opaque_compiler_type_t type) { return false; }

bool TypeSystemCpp::IsFloatingPointType(opaque_compiler_type_t type) {
  return type && GetCppType(type)->GetEncoding() == eEncodingIEEE754;
}

bool TypeSystemCpp::IsFunctionType(opaque_compiler_type_t type) {
  return type &&
         llvm::isa<cpp_typesystem::FunctionType>(Desugar(GetCppType(type)));
}

size_t
TypeSystemCpp::GetNumberOfFunctionArguments(opaque_compiler_type_t type) {
  return 0;
}

CompilerType
TypeSystemCpp::GetFunctionArgumentAtIndex(opaque_compiler_type_t type,
                                          const size_t index) {
  return CompilerType();
}

bool TypeSystemCpp::IsFunctionPointerType(opaque_compiler_type_t type) {
  return false;
}

bool TypeSystemCpp::IsMemberFunctionPointerType(opaque_compiler_type_t type) {
  return false;
}

bool TypeSystemCpp::IsMemberDataPointerType(opaque_compiler_type_t type) {
  return false;
}

bool TypeSystemCpp::IsBlockPointerType(
    opaque_compiler_type_t type, CompilerType *function_pointer_type_ptr) {
  return false;
}

bool TypeSystemCpp::IsIntegerType(opaque_compiler_type_t type,
                                  bool &is_signed) {
  is_signed = false;
  if (!type)
    return false;
  switch (GetCppType(type)->GetEncoding()) {
  case eEncodingSint:
    is_signed = true;
    return true;
  case eEncodingUint:
    return true;
  default:
    return false;
  }
}

bool TypeSystemCpp::IsScopedEnumerationType(opaque_compiler_type_t type) {
  if (!type)
    return false;
  if (auto *enum_type =
          llvm::dyn_cast<cpp_typesystem::EnumType>(Desugar(GetCppType(type))))
    return enum_type->IsScoped();
  return false;
}

bool TypeSystemCpp::IsPossibleDynamicType(opaque_compiler_type_t type,
                                          CompilerType *target_type,
                                          bool check_cplusplus,
                                          bool check_objc) {
  return false;
}

bool TypeSystemCpp::IsPointerType(opaque_compiler_type_t type,
                                  CompilerType *pointee_type) {
  if (pointee_type)
    pointee_type->Clear();
  auto *ptr = llvm::dyn_cast_or_null<cpp_typesystem::PointerType>(
      type ? Desugar(GetCppType(type)) : nullptr);
  if (!ptr)
    return false;
  if (pointee_type)
    *pointee_type = GetCompilerType(ptr->GetPointeeType());
  return true;
}

bool TypeSystemCpp::IsScalarType(opaque_compiler_type_t type) {
  return type && (GetCppType(type)->GetTypeInfo() & eTypeIsScalar);
}

bool TypeSystemCpp::IsVoidType(opaque_compiler_type_t type) { return false; }

bool TypeSystemCpp::CanPassInRegisters(const CompilerType &type) {
  return false;
}

bool TypeSystemCpp::SupportsLanguage(LanguageType language) {
  return Language::LanguageIsCFamily(language);
}

bool TypeSystemCpp::GetCompleteType(opaque_compiler_type_t type) {
  if (!type)
    return false;
  // See through typedefs/qualifiers: it is the underlying record that carries
  // completion state and is registered in the forward-declaration map.
  cpp_typesystem::Type *t = Desugar(GetCppType(type));
  if (!t)
    return false;
  if (t->IsComplete())
    return true;
  // Ask our SymbolFile to fill in the members from the debug info.
  if (SymbolFile *sym_file = GetSymbolFile()) {
    CompilerType ct = GetCompilerType(t);
    sym_file->CompleteType(ct);
  }
  return t->IsComplete();
}

void TypeSystemCpp::CompleteMemberFunctions(cpp_typesystem::Type *type) {
  if (!type)
    return;
  // See through typedefs/qualifiers to the underlying record, mirroring
  // GetCompleteType.
  auto *record =
      llvm::dyn_cast_or_null<cpp_typesystem::RecordType>(Desugar(type));
  if (!record || record->AreMemberFunctionsParsed())
    return;
  // Member functions live on the completed record, and the DWARF parser learns
  // the record's defining DIE while completing it, so complete it first.
  GetCompleteType(record);
  if (auto *parser =
          llvm::dyn_cast_or_null<DWARFASTParserCpp>(GetDWARFParser()))
    parser->CompleteMemberFunctionsFromDWARF(*record);
}

uint32_t TypeSystemCpp::GetPointerByteSize() {
  return m_context.GetLanguageOpts().GetBuiltinSizes().pointer_size;
}

CompilerType TypeSystemCpp::GetPointerDiffType(bool is_signed) {
  return CompilerType();
}

unsigned TypeSystemCpp::GetPtrAuthKey(opaque_compiler_type_t type) { return 0; }

unsigned TypeSystemCpp::GetPtrAuthDiscriminator(opaque_compiler_type_t type) {
  return 0;
}

bool TypeSystemCpp::GetPtrAuthAddressDiversity(opaque_compiler_type_t type) {
  return false;
}

ConstString TypeSystemCpp::GetTypeName(opaque_compiler_type_t type,
                                       bool BaseOnly) {
  if (!type)
    return ConstString();
  cpp_typesystem::Type *t = GetCppType(type);
  // Arrays have no name of their own; build "<element>[<count>]". For a
  // multidimensional array the nesting is outermost-dimension first, so peel
  // the whole chain and print the dimensions in source order after the
  // innermost element's name.
  if (llvm::isa<cpp_typesystem::ArrayType>(t)) {
    std::string dims;
    cpp_typesystem::Type *cur = t;
    while (auto *array = llvm::dyn_cast<cpp_typesystem::ArrayType>(cur)) {
      if (std::optional<uint64_t> num_elements = array->GetNumElements())
        dims += llvm::formatv("[{0}]", *num_elements).str();
      else
        dims += "[]";
      cur = array->GetElementType();
    }
    std::string element_name = GetTypeName(cur, BaseOnly).GetStringRef().str();
    return ConstString(element_name + dims);
  }
  // Function types and pointers/references to them need C declarator syntax
  // (`int (*)(const char *)`); BuildDisplayName already produces it.
  if (llvm::isa<cpp_typesystem::FunctionType>(t))
    return ConstString(BuildDisplayName(t));
  // Pointers have no name of their own either; build "<pointee> *".
  if (auto *ptr = llvm::dyn_cast<cpp_typesystem::PointerType>(t)) {
    cpp_typesystem::Type *pointee = ptr->GetPointeeType();
    if (llvm::isa_and_nonnull<cpp_typesystem::FunctionType>(pointee))
      return ConstString(BuildDisplayName(t));
    std::string pointee_name =
        pointee ? GetTypeName(pointee, BaseOnly).GetStringRef().str() : "void";
    return ConstString(llvm::formatv("{0} *", pointee_name).str());
  }
  // References likewise: "<pointee> &" or "<pointee> &&".
  if (auto *ref = llvm::dyn_cast<cpp_typesystem::ReferenceType>(t)) {
    cpp_typesystem::Type *pointee = ref->GetPointeeType();
    if (llvm::isa_and_nonnull<cpp_typesystem::FunctionType>(pointee))
      return ConstString(BuildDisplayName(t));
    std::string pointee_name =
        pointee ? GetTypeName(pointee, BaseOnly).GetStringRef().str() : "void";
    return ConstString(
        llvm::formatv("{0} {1}", pointee_name, ref->IsRValue() ? "&&" : "&")
            .str());
  }
  // cv-qualified types render as "const"/"volatile" prefixing the unqualified
  // name.
  if (auto *cv = llvm::dyn_cast<cpp_typesystem::CVQualifiedType>(t)) {
    std::string underlying_name =
        cv->GetUnderlyingType() ? GetTypeName(cv->GetUnderlyingType(), BaseOnly)
                                      .GetStringRef()
                                      .str()
                                : "";
    std::string result;
    if (cv->IsConst())
      result += "const ";
    if (cv->IsVolatile())
      result += "volatile ";
    result += underlying_name;
    return ConstString(result);
  }
  return ConstString(t->GetName().GetName());
}

ConstString TypeSystemCpp::GetDisplayTypeName(opaque_compiler_type_t type) {
  if (!type)
    return ConstString();
  return ConstString(BuildDisplayName(GetCppType(type)));
}

uint32_t
TypeSystemCpp::GetTypeInfo(opaque_compiler_type_t type,
                           CompilerType *pointee_or_element_compiler_type) {
  if (pointee_or_element_compiler_type)
    pointee_or_element_compiler_type->Clear();
  if (!type)
    return 0;
  cpp_typesystem::Type *t = GetCppType(type);
  // Hand back the element/pointee type when asked; callers such as
  // ValueObject::GetPointeeData rely on it to know how to read array elements
  // or dereference pointers/references.
  if (pointee_or_element_compiler_type) {
    cpp_typesystem::Type *inner = nullptr;
    if (auto *array = llvm::dyn_cast<cpp_typesystem::ArrayType>(Desugar(t)))
      inner = array->GetElementType();
    else if (auto *ptr =
                 llvm::dyn_cast<cpp_typesystem::PointerType>(Desugar(t)))
      inner = ptr->GetPointeeType();
    else if (auto *ref =
                 llvm::dyn_cast<cpp_typesystem::ReferenceType>(Desugar(t)))
      inner = ref->GetPointeeType();
    if (inner)
      *pointee_or_element_compiler_type = GetCompilerType(inner);
  }
  return t->GetTypeInfo();
}

LanguageType TypeSystemCpp::GetMinimumLanguage(opaque_compiler_type_t type) {
  // TypeSystemCpp models C/C++/Objective-C++ types. Reporting C++ is what puts
  // types in the C++ formatter category, so that e.g. the libc++ container
  // data formatters are consulted (see FormatManager::GetCandidateLanguages).
  return eLanguageTypeC_plus_plus;
}

TypeClass TypeSystemCpp::GetTypeClass(opaque_compiler_type_t type) {
  if (!type)
    return eTypeClassInvalid;
  return GetCppType(type)->GetTypeClass();
}

CompilerType
TypeSystemCpp::GetArrayElementType(opaque_compiler_type_t type,
                                   ExecutionContextScope *exe_scope) {
  if (!type)
    return CompilerType();
  if (auto *array =
          llvm::dyn_cast<cpp_typesystem::ArrayType>(Desugar(GetCppType(type))))
    return GetCompilerType(array->GetElementType());
  return CompilerType();
}

CompilerType TypeSystemCpp::GetCanonicalType(opaque_compiler_type_t type) {
  if (!type)
    return CompilerType();
  // The canonical type is the type with all typedef/cv sugar stripped.
  return GetCompilerType(Desugar(GetCppType(type)));
}

CompilerType
TypeSystemCpp::GetEnumerationIntegerType(opaque_compiler_type_t type) {
  if (!type)
    return CompilerType();
  if (auto *enum_type =
          llvm::dyn_cast<cpp_typesystem::EnumType>(Desugar(GetCppType(type))))
    return GetCompilerType(enum_type->GetUnderlyingType());
  return CompilerType();
}

void TypeSystemCpp::ForEachEnumerator(
    opaque_compiler_type_t type,
    std::function<bool(const CompilerType &integer_type, ConstString name,
                       const llvm::APSInt &value)> const &callback) {
  if (!type)
    return;
  auto *enum_type =
      llvm::dyn_cast<cpp_typesystem::EnumType>(Desugar(GetCppType(type)));
  if (!enum_type)
    return;

  CompilerType integer_type = GetCompilerType(enum_type->GetUnderlyingType());
  const bool is_signed = enum_type->IsSigned();
  // Enumerator values are stored as raw bits; recover their APSInt using the
  // underlying type's width so signed values sign-extend correctly.
  unsigned bit_width = 64;
  if (std::optional<uint64_t> byte_size =
          llvm::expectedToOptional(integer_type.GetByteSize(nullptr)))
    bit_width = static_cast<unsigned>(*byte_size * 8);

  for (const cpp_typesystem::Enumerator &enumerator :
       enum_type->GetEnumerators()) {
    llvm::APSInt value(llvm::APInt(bit_width, enumerator.value, is_signed),
                       !is_signed);
    if (!callback(integer_type, ConstString(enumerator.name.GetName()), value))
      break;
  }
}

int TypeSystemCpp::GetFunctionArgumentCount(opaque_compiler_type_t type) {
  return -1;
}

CompilerType
TypeSystemCpp::GetFunctionArgumentTypeAtIndex(opaque_compiler_type_t type,
                                              size_t idx) {
  return CompilerType();
}

CompilerType TypeSystemCpp::GetFunctionReturnType(opaque_compiler_type_t type) {
  return CompilerType();
}

size_t TypeSystemCpp::GetNumMemberFunctions(opaque_compiler_type_t type) {
  return 0;
}

TypeMemberFunctionImpl
TypeSystemCpp::GetMemberFunctionAtIndex(opaque_compiler_type_t type,
                                        size_t idx) {
  return TypeMemberFunctionImpl();
}

CompilerType TypeSystemCpp::GetPointeeType(opaque_compiler_type_t type) {
  if (!type)
    return CompilerType();
  if (auto *ptr = llvm::dyn_cast<cpp_typesystem::PointerType>(
          Desugar(GetCppType(type))))
    return GetCompilerType(ptr->GetPointeeType());
  return CompilerType();
}

CompilerType TypeSystemCpp::GetPointerType(opaque_compiler_type_t type) {
  if (!type)
    return CompilerType();
  return cpp_typesystem::Builder(*this).CreatePointerType(
      GetCompilerType(GetCppType(type)));
}

const llvm::fltSemantics &TypeSystemCpp::GetFloatTypeSemantics(size_t byte_size,
                                                               Format format) {
  return m_context.GetLanguageOpts().GetFloatTypeSemantics(byte_size, format);
}

llvm::Expected<uint64_t>
TypeSystemCpp::GetBitSize(opaque_compiler_type_t type,
                          ExecutionContextScope *exe_scope) {
  if (!type)
    return llvm::createStringError("invalid type");
  if (std::optional<uint64_t> byte_size = GetCppType(type)->GetByteSize())
    return *byte_size * 8;
  return llvm::createStringError("TypeSystemCpp::GetBitSize: unknown size");
}

Encoding TypeSystemCpp::GetEncoding(opaque_compiler_type_t type) {
  if (!type)
    return eEncodingInvalid;
  return GetCppType(type)->GetEncoding();
}

Format TypeSystemCpp::GetFormat(opaque_compiler_type_t type) {
  if (!type)
    return eFormatDefault;
  return GetCppType(type)->GetFormat();
}

llvm::Expected<uint32_t>
TypeSystemCpp::GetNumChildren(opaque_compiler_type_t type,
                              bool omit_empty_base_classes,
                              const ExecutionContext *exe_ctx) {
  if (!type)
    return 0;
  cpp_typesystem::Type *t = Desugar(GetCppType(type));
  // An array's children are its elements.
  if (auto *array = llvm::dyn_cast<cpp_typesystem::ArrayType>(t))
    return array->GetNumElements().value_or(0);
  // A pointer's children are those of its pointee: expanding a pointer to an
  // aggregate shows the aggregate's members directly, while a pointer to a
  // scalar has a single child (the dereferenced value). `void *` has none.
  // Looking through to the pointee's members must not *complete* it, though: a
  // type reachable only through a pointer stays a forward declaration until it
  // is explicitly dereferenced/accessed (see the note in
  // GetChildCompilerTypeAtIndex). So only expand an already-complete pointee.
  if (auto *ptr = llvm::dyn_cast<cpp_typesystem::PointerType>(t)) {
    cpp_typesystem::Type *pointee = ptr->GetPointeeType();
    if (!pointee)
      return 0;
    if (pointee->IsAggregate() && pointee->IsComplete())
      return GetNumChildren(pointee, omit_empty_base_classes, exe_ctx);
    return 1;
  }
  // A reference is transparent: its children are those of the referenced type.
  // Like pointers, counting them must not force completion of the referent.
  if (auto *ref = llvm::dyn_cast<cpp_typesystem::ReferenceType>(t)) {
    cpp_typesystem::Type *pointee = ref->GetPointeeType();
    if (!pointee)
      return 0;
    if (pointee->IsAggregate() && pointee->IsComplete())
      return GetNumChildren(pointee, omit_empty_base_classes, exe_ctx);
    return 1;
  }
  if (!t->IsAggregate())
    return 0;
  GetCompleteType(type);
  // Children of a record are its direct base classes followed by its fields.
  return t->GetNumBaseClasses() + t->GetNumFields();
}

BasicType TypeSystemCpp::GetBasicTypeEnumeration(opaque_compiler_type_t type) {
  return eBasicTypeInvalid;
}

uint32_t TypeSystemCpp::GetNumFields(opaque_compiler_type_t type) {
  if (!type)
    return 0;
  GetCompleteType(type);
  return GetCppType(type)->GetNumFields();
}

CompilerType TypeSystemCpp::GetFieldAtIndex(opaque_compiler_type_t type,
                                            size_t idx, std::string &name,
                                            uint64_t *bit_offset_ptr,
                                            uint32_t *bitfield_bit_size_ptr,
                                            bool *is_bitfield_ptr) {
  if (!type)
    return CompilerType();
  GetCompleteType(type);
  const Field *field = GetCppType(type)->GetFieldAtIndex(idx);
  if (!field)
    return CompilerType();
  name = field->name.GetName().str();
  if (bit_offset_ptr)
    *bit_offset_ptr = field->byte_offset * 8 + field->bitfield_bit_offset;
  if (bitfield_bit_size_ptr)
    *bitfield_bit_size_ptr = field->bitfield_bit_size;
  if (is_bitfield_ptr)
    *is_bitfield_ptr = field->IsBitfield();
  return GetCompilerType(field->type.Get());
}

uint32_t TypeSystemCpp::GetNumDirectBaseClasses(opaque_compiler_type_t type) {
  if (!type)
    return 0;
  GetCompleteType(type);
  return GetCppType(type)->GetNumBaseClasses();
}

uint32_t TypeSystemCpp::GetNumVirtualBaseClasses(opaque_compiler_type_t type) {
  return 0;
}

CompilerType
TypeSystemCpp::GetDirectBaseClassAtIndex(opaque_compiler_type_t type,
                                         size_t idx, uint32_t *bit_offset_ptr) {
  if (!type)
    return CompilerType();
  GetCompleteType(type);
  const cpp_typesystem::BaseClass *base =
      GetCppType(type)->GetBaseClassAtIndex(idx);
  if (!base)
    return CompilerType();
  if (bit_offset_ptr)
    *bit_offset_ptr = base->byte_offset * 8;
  return GetCompilerType(base->type.Get());
}

CompilerType TypeSystemCpp::GetVirtualBaseClassAtIndex(
    opaque_compiler_type_t type, size_t idx, uint32_t *bit_offset_ptr) {
  return CompilerType();
}

llvm::Expected<CompilerType> TypeSystemCpp::GetDereferencedType(
    opaque_compiler_type_t type, ExecutionContext *exe_ctx,
    std::string &deref_name, uint32_t &deref_byte_size,
    int32_t &deref_byte_offset, ValueObject *valobj, uint64_t &language_flags) {
  // Only pointers, references and arrays can be dereferenced.
  if (!IsPointerOrReferenceType(type, nullptr) &&
      !IsArrayType(type, nullptr, nullptr, nullptr))
    return llvm::createStringError("not a pointer, reference or array type");

  // The dereferenced value is child 0. Ask for it non-transparently so a
  // pointer-to-aggregate yields the pointee itself rather than its members.
  uint32_t child_bitfield_bit_size = 0;
  uint32_t child_bitfield_bit_offset = 0;
  bool child_is_base_class = false;
  bool child_is_deref_of_parent = false;
  return GetChildCompilerTypeAtIndex(
      type, exe_ctx, /*idx=*/0, /*transparent_pointers=*/false,
      /*omit_empty_base_classes=*/true, /*ignore_array_bounds=*/false,
      deref_name, deref_byte_size, deref_byte_offset, child_bitfield_bit_size,
      child_bitfield_bit_offset, child_is_base_class, child_is_deref_of_parent,
      valobj, language_flags);
}

llvm::Expected<CompilerType> TypeSystemCpp::GetChildCompilerTypeAtIndex(
    opaque_compiler_type_t type, ExecutionContext *exe_ctx, size_t idx,
    bool transparent_pointers, bool omit_empty_base_classes,
    bool ignore_array_bounds, std::string &child_name,
    uint32_t &child_byte_size, int32_t &child_byte_offset,
    uint32_t &child_bitfield_bit_size, uint32_t &child_bitfield_bit_offset,
    bool &child_is_base_class, bool &child_is_deref_of_parent,
    ValueObject *valobj, uint64_t &language_flags) {
  child_name.clear();
  child_byte_size = 0;
  child_byte_offset = 0;
  child_bitfield_bit_size = 0;
  child_bitfield_bit_offset = 0;
  child_is_base_class = false;
  child_is_deref_of_parent = false;
  language_flags = 0;

  if (!type)
    return CompilerType();
  GetCompleteType(type);
  cpp_typesystem::Type *t = Desugar(GetCppType(type));

  // Array elements: child N is the element at offset N * element_size.
  if (auto *array = llvm::dyn_cast<cpp_typesystem::ArrayType>(t)) {
    if (!ignore_array_bounds) {
      std::optional<uint64_t> num_elements = array->GetNumElements();
      if (num_elements && idx >= *num_elements)
        return CompilerType();
    }
    cpp_typesystem::Type *element_type = array->GetElementType();
    child_name = llvm::formatv("[{0}]", idx).str();
    if (std::optional<uint64_t> byte_size = element_type->GetByteSize()) {
      child_byte_size = *byte_size;
      child_byte_offset = idx * *byte_size;
    }
    return GetCompilerType(element_type);
  }

  // A pointer's children are those of its pointee. Expanding a pointer to an
  // aggregate transparently shows the pointee's members; otherwise the single
  // child is the dereferenced value.
  if (auto *ptr = llvm::dyn_cast<cpp_typesystem::PointerType>(t)) {
    cpp_typesystem::Type *pointee = ptr->GetPointeeType();
    if (!pointee)
      return CompilerType(); // Can't dereference `void *`.

    // Only look through to the pointee's members if it is already complete.
    // Doing so must never *cause* completion: a type reachable only through a
    // pointer stays a forward declaration until it is explicitly dereferenced
    // or a member is accessed. An incomplete pointee is treated like a scalar
    // one -- its single child is the dereferenced value (which, when actually
    // materialized, completes it), matching GetNumChildren.
    if (transparent_pointers && pointee->IsAggregate() &&
        pointee->IsComplete()) {
      bool tmp_child_is_deref_of_parent = false;
      return GetCompilerType(pointee).GetChildCompilerTypeAtIndex(
          exe_ctx, idx, transparent_pointers, omit_empty_base_classes,
          ignore_array_bounds, child_name, child_byte_size, child_byte_offset,
          child_bitfield_bit_size, child_bitfield_bit_offset,
          child_is_base_class, tmp_child_is_deref_of_parent, valobj,
          language_flags);
    }

    child_is_deref_of_parent = true;
    if (const char *parent_name =
            valobj ? valobj->GetName().GetCString() : nullptr) {
      child_name.assign(1, '*');
      child_name += parent_name;
    }
    if (idx != 0)
      return CompilerType();
    if (std::optional<uint64_t> byte_size = pointee->GetByteSize())
      child_byte_size = *byte_size;
    child_byte_offset = 0;
    return GetCompilerType(pointee);
  }

  // A reference is transparent, just like a pointer: expanding it either shows
  // the referenced aggregate's members or the single referenced value.
  if (auto *ref = llvm::dyn_cast<cpp_typesystem::ReferenceType>(t)) {
    cpp_typesystem::Type *pointee = ref->GetPointeeType();
    if (!pointee)
      return CompilerType();

    // As with pointers, only expand an already-complete referent transparently
    // so that merely inspecting the reference doesn't force its completion.
    if (transparent_pointers && pointee->IsAggregate() &&
        pointee->IsComplete()) {
      bool tmp_child_is_deref_of_parent = false;
      return GetCompilerType(pointee).GetChildCompilerTypeAtIndex(
          exe_ctx, idx, transparent_pointers, omit_empty_base_classes,
          ignore_array_bounds, child_name, child_byte_size, child_byte_offset,
          child_bitfield_bit_size, child_bitfield_bit_offset,
          child_is_base_class, tmp_child_is_deref_of_parent, valobj,
          language_flags);
    }

    child_is_deref_of_parent = true;
    if (const char *parent_name =
            valobj ? valobj->GetName().GetCString() : nullptr) {
      child_name.assign(1, '&');
      child_name += parent_name;
    }
    if (idx != 0)
      return CompilerType();
    if (std::optional<uint64_t> byte_size = pointee->GetByteSize())
      child_byte_size = *byte_size;
    child_byte_offset = 0;
    return GetCompilerType(pointee);
  }

  // Children are laid out as the direct base classes followed by the fields.
  uint32_t num_bases = t->GetNumBaseClasses();
  if (idx < num_bases) {
    const cpp_typesystem::BaseClass *base = t->GetBaseClassAtIndex(idx);
    if (!base)
      return CompilerType();
    child_name = base->type.Get()->GetName().GetName().str();
    child_byte_offset = base->byte_offset;
    if (std::optional<uint64_t> byte_size = base->type.Get()->GetByteSize())
      child_byte_size = *byte_size;
    child_is_base_class = true;
    return GetCompilerType(base->type.Get());
  }

  const Field *field = t->GetFieldAtIndex(idx - num_bases);
  if (!field)
    return CompilerType();

  child_name = field->name.GetName().str();
  child_byte_offset = field->byte_offset;
  if (std::optional<uint64_t> byte_size = field->type.Get()->GetByteSize())
    child_byte_size = *byte_size;
  if (field->IsBitfield()) {
    child_bitfield_bit_size = field->bitfield_bit_size;
    child_bitfield_bit_offset = field->bitfield_bit_offset;
  }
  return GetCompilerType(field->type.Get());
}

llvm::Expected<uint32_t>
TypeSystemCpp::GetIndexOfChildWithName(opaque_compiler_type_t type,
                                       llvm::StringRef name,
                                       bool omit_empty_base_classes) {
  if (!type)
    return llvm::createStringError("invalid type");
  GetCompleteType(type);
  cpp_typesystem::Type *t = Desugar(GetCppType(type));
  // A pointer or reference forwards child lookup to its (aggregate) pointee.
  cpp_typesystem::Type *pointee = nullptr;
  if (auto *ptr = llvm::dyn_cast<cpp_typesystem::PointerType>(t))
    pointee = ptr->GetPointeeType();
  else if (auto *ref = llvm::dyn_cast<cpp_typesystem::ReferenceType>(t))
    pointee = ref->GetPointeeType();
  if (pointee) {
    if (pointee->IsAggregate())
      return GetCompilerType(pointee).GetIndexOfChildWithName(
          name, omit_empty_base_classes);
    return llvm::createStringError(
        "TypeSystemCpp::GetIndexOfChildWithName: no such child");
  }
  uint32_t num_bases = t->GetNumBaseClasses();
  // Base classes are the first children; match them by their type name.
  for (uint32_t i = 0; i < num_bases; ++i) {
    const cpp_typesystem::BaseClass *base = t->GetBaseClassAtIndex(i);
    if (base->type.Get()->GetName().GetName() == name)
      return i;
  }
  // Fields follow the base classes.
  for (uint32_t i = 0, e = t->GetNumFields(); i < e; ++i) {
    if (t->GetFieldAtIndex(i)->name.GetName() == name)
      return num_bases + i;
  }
  return llvm::createStringError(
      "TypeSystemCpp::GetIndexOfChildWithName: no such child");
}

size_t TypeSystemCpp::GetIndexOfChildMemberWithName(
    opaque_compiler_type_t type, llvm::StringRef name,
    bool omit_empty_base_classes, std::vector<uint32_t> &child_indexes) {
  if (!type)
    return 0;
  GetCompleteType(type);
  cpp_typesystem::Type *t = Desugar(GetCppType(type));
  // A pointer or reference forwards member lookup to its (aggregate) pointee,
  // so that e.g. `ptr->member` / `ref.member` resolves against the pointed-to
  // record.
  cpp_typesystem::Type *pointee = nullptr;
  if (auto *ptr = llvm::dyn_cast<cpp_typesystem::PointerType>(t))
    pointee = ptr->GetPointeeType();
  else if (auto *ref = llvm::dyn_cast<cpp_typesystem::ReferenceType>(t))
    pointee = ref->GetPointeeType();
  if (pointee) {
    if (pointee->IsAggregate())
      return GetCompilerType(pointee).GetIndexOfChildMemberWithName(
          name, omit_empty_base_classes, child_indexes);
    return 0;
  }
  uint32_t num_bases = t->GetNumBaseClasses();

  // A matching field is a direct child, laid out after the base classes. An
  // unnamed field is an anonymous union/struct whose members are reached as if
  // they belonged to this record, so recurse into it.
  for (uint32_t i = 0, e = t->GetNumFields(); i < e; ++i) {
    const Field *field = t->GetFieldAtIndex(i);
    llvm::StringRef field_name = field->name.GetName();
    if (field_name == name) {
      child_indexes.push_back(num_bases + i);
      return child_indexes.size();
    }
    if (field_name.empty() && field->type) {
      std::vector<uint32_t> save_indices = child_indexes;
      child_indexes.push_back(num_bases + i);
      if (GetCompilerType(field->type.Get())
              .GetIndexOfChildMemberWithName(name, omit_empty_base_classes,
                                             child_indexes))
        return child_indexes.size();
      child_indexes = std::move(save_indices);
    }
  }

  // Otherwise the member may be inherited from a base class. Base classes are
  // the first children, so their child index is just their position.
  for (uint32_t i = 0; i < num_bases; ++i) {
    const cpp_typesystem::BaseClass *base = t->GetBaseClassAtIndex(i);
    std::vector<uint32_t> save_indices = child_indexes;
    child_indexes.push_back(i);
    if (GetCompilerType(base->type.Get())
            .GetIndexOfChildMemberWithName(name, omit_empty_base_classes,
                                           child_indexes))
      return child_indexes.size();
    child_indexes = std::move(save_indices);
  }
  return 0;
}

#ifndef NDEBUG
LLVM_DUMP_METHOD void TypeSystemCpp::dump(opaque_compiler_type_t type) const {}
#endif

/// Render an enum value as an enumerator name when it matches one exactly. For
/// a "bitfield/flag" enum (every enumerator is a single bit or a union of
/// previously-seen bits) a combined value is decomposed into `A | B`. Mirrors
/// TypeSystemClang's DumpEnumValue.
static bool DumpEnumValue(const cpp_typesystem::EnumType &enum_type, Stream &s,
                          const DataExtractor &data, lldb::offset_t byte_offset,
                          size_t byte_size) {
  lldb::offset_t offset = byte_offset;
  const bool is_signed = enum_type.IsSigned();
  const uint64_t enum_svalue =
      is_signed ? static_cast<uint64_t>(
                      data.GetMaxS64Bitfield(&offset, byte_size, 0, 0))
                : data.GetMaxU64Bitfield(&offset, byte_size, 0, 0);

  bool can_be_bitfield = true;
  uint64_t covered_bits = 0;
  int num_enumerators = 0;

  // Look for an exact match while applying the bitfield heuristic: an enum is
  // likely a flag set if every enumerator is a single bit or a superset of the
  // bits seen so far.
  const std::vector<cpp_typesystem::Enumerator> &enumerators =
      enum_type.GetEnumerators();
  if (enumerators.empty())
    can_be_bitfield = false;
  for (const cpp_typesystem::Enumerator &enumerator : enumerators) {
    uint64_t val = enumerator.value;
    if (is_signed)
      val = llvm::SignExtend64(val, 8 * byte_size);
    if (llvm::popcount(val) != 1 && (val & ~covered_bits) != 0)
      can_be_bitfield = false;
    covered_bits |= val;
    ++num_enumerators;
    if (enumerator.value == enum_svalue) {
      s.PutCString(enumerator.name.GetName());
      return true;
    }
  }

  // Unsigned values make more sense for flags.
  offset = byte_offset;
  const uint64_t enum_uvalue =
      data.GetMaxU64Bitfield(&offset, byte_size, 0, 0);

  // No exact match and this isn't a flag enum: print the value numerically.
  if (!can_be_bitfield) {
    if (is_signed)
      s.Printf("%" PRIi64, static_cast<int64_t>(enum_svalue));
    else
      s.Printf("%" PRIu64, enum_uvalue);
    return true;
  }

  if (!enum_uvalue) {
    // Flag enum, but the value is 0 and matched no enumerator above.
    s.Printf("0x%" PRIx64, enum_uvalue);
    return true;
  }

  uint64_t remaining_value = enum_uvalue;
  std::vector<std::pair<uint64_t, llvm::StringRef>> values;
  values.reserve(num_enumerators);
  for (const cpp_typesystem::Enumerator &enumerator : enumerators)
    if (enumerator.value)
      values.emplace_back(enumerator.value, enumerator.name.GetName());

  // Sort by descending population count (stably) so that in
  // `enum { A, B, ALL = A|B }` we emit ALL before A/B, and `A | C` keeps the
  // declaration order for equal popcounts.
  llvm::stable_sort(values, [](const auto &a, const auto &b) {
    return llvm::popcount(a.first) > llvm::popcount(b.first);
  });

  for (const auto &val : values) {
    if ((remaining_value & val.first) != val.first)
      continue;
    remaining_value &= ~val.first;
    s.PutCString(val.second);
    if (remaining_value)
      s.PutCString(" | ");
  }

  // Print any bits not covered by an enumerator as hex.
  if (remaining_value)
    s.Printf("0x%" PRIx64, remaining_value);

  return true;
}

bool TypeSystemCpp::DumpTypeValue(opaque_compiler_type_t type, Stream &s,
                                  Format format, const DataExtractor &data,
                                  offset_t data_offset, size_t data_byte_size,
                                  uint32_t bitfield_bit_size,
                                  uint32_t bitfield_bit_offset,
                                  ExecutionContextScope *exe_scope) {
  if (!type)
    return false;
  cpp_typesystem::Type *t = Desugar(GetCppType(type));
  // Aggregates don't have a scalar value to print; their children are dumped
  // individually.
  if (t->IsAggregate())
    return false;
  if (format == eFormatDefault)
    format = t->GetFormat();
  // Enumerations: show the enumerator name when possible rather than the raw
  // integer value.
  if (auto *enum_type = llvm::dyn_cast<cpp_typesystem::EnumType>(t)) {
    if ((format == eFormatEnum || format == eFormatDefault) &&
        bitfield_bit_size == 0)
      return DumpEnumValue(*enum_type, s, data, data_offset, data_byte_size);
  }
  return DumpDataExtractor(data, &s, data_offset, format, data_byte_size,
                           /*item_count=*/1, UINT32_MAX, LLDB_INVALID_ADDRESS,
                           bitfield_bit_size, bitfield_bit_offset, exe_scope);
}

void TypeSystemCpp::DumpTypeDescription(opaque_compiler_type_t type,
                                        DescriptionLevel level) {
  StreamFile s(stdout, false);
  DumpTypeDescription(type, s, level);
}

// Append a C declarator ("<type> <name>") for a record member, using array
// declarator syntax (`char padding[0]`) when the member's type is an array so
// the printed definition matches C source form.
static void AppendMemberDecl(Stream &s, TypeSystemCpp &ts,
                             cpp_typesystem::Type *field_type,
                             llvm::StringRef name) {
  using namespace cpp_typesystem;
  std::string dims;
  cpp_typesystem::Type *cur = field_type;
  while (auto *array = llvm::dyn_cast_or_null<ArrayType>(cur)) {
    if (std::optional<uint64_t> n = array->GetNumElements())
      dims += llvm::formatv("[{0}]", *n).str();
    else
      dims += "[]";
    cur = array->GetElementType();
  }
  std::string base = ts.GetTypeName(cur, /*BaseOnly=*/false).GetStringRef().str();
  s << base;
  if (!name.empty())
    s << " " << name;
  s << dims;
}

void TypeSystemCpp::DumpTypeDescription(opaque_compiler_type_t type, Stream &s,
                                        DescriptionLevel level) {
  if (!type)
    return;
  cpp_typesystem::Type *t = Desugar(GetCppType(type));

  if (auto *enum_type = llvm::dyn_cast<cpp_typesystem::EnumType>(t)) {
    GetCompleteType(type);
    s.Printf("enum %s {\n", GetTypeName(t, /*BaseOnly=*/false).GetCString());
    for (const cpp_typesystem::Enumerator &e : enum_type->GetEnumerators()) {
      if (enum_type->IsSigned())
        s.Printf("    %s = %" PRId64 ",\n", e.name.GetName().str().c_str(),
                 static_cast<int64_t>(e.value));
      else
        s.Printf("    %s = %" PRIu64 ",\n", e.name.GetName().str().c_str(),
                 e.value);
    }
    s.PutCString("}");
    return;
  }

  if (auto *record = llvm::dyn_cast<cpp_typesystem::RecordType>(t)) {
    GetCompleteType(type);
    const char *tag =
        record->IsUnion()
            ? "union"
            : (llvm::isa<cpp_typesystem::ClassType>(record) ? "class"
                                                            : "struct");
    s.Printf("%s %s {\n", tag, GetTypeName(t, /*BaseOnly=*/false).GetCString());
    for (uint32_t i = 0, e = record->GetNumFields(); i != e; ++i) {
      const cpp_typesystem::Field *field = record->GetFieldAtIndex(i);
      if (!field)
        continue;
      s.PutCString("    ");
      AppendMemberDecl(s, *this, field->type.Get(), field->name.GetName());
      if (field->IsBitfield())
        s.Printf(" : %u", field->bitfield_bit_size);
      s.PutCString(";\n");
    }
    s.PutCString("}");
    return;
  }

  // Anything else: just print its name.
  if (ConstString name = GetTypeName(t, /*BaseOnly=*/false))
    s.PutCString(name.GetStringRef());
}

void TypeSystemCpp::Dump(llvm::raw_ostream &output, llvm::StringRef filter,
                         bool show_color) {}

bool TypeSystemCpp::IsRuntimeGeneratedType(opaque_compiler_type_t type) {
  return false;
}

bool TypeSystemCpp::IsPointerOrReferenceType(opaque_compiler_type_t type,
                                             CompilerType *pointee_type) {
  if (IsPointerType(type, pointee_type))
    return true;
  return IsReferenceType(type, pointee_type, /*is_rvalue=*/nullptr);
}

unsigned TypeSystemCpp::GetTypeQualifiers(opaque_compiler_type_t type) {
  return 0;
}

std::optional<size_t>
TypeSystemCpp::GetTypeBitAlign(opaque_compiler_type_t type,
                              ExecutionContextScope *exe_scope) {
  if (!type)
    return std::nullopt;
  cpp_typesystem::Type *t = Desugar(GetCppType(type));
  if (!t)
    return std::nullopt;

  // Pointers and references are pointer-aligned.
  if (llvm::isa<cpp_typesystem::PointerType>(t) ||
      llvm::isa<cpp_typesystem::ReferenceType>(t))
    return GetPointerByteSize() * 8;

  // The cpp_typesystem model doesn't record alignment. For scalars this is the
  // size; for aggregates, derive an alignment that divides the size (natural
  // alignment for the standard-layout types produced from debug info). Cap at
  // 8 bytes, which matches the fundamental alignment on the supported targets.
  std::optional<uint64_t> byte_size = t->GetByteSize();
  if (!byte_size || *byte_size == 0)
    return std::nullopt;
  uint64_t align_bytes = 1;
  while (align_bytes * 2 <= 8 && (*byte_size % (align_bytes * 2)) == 0)
    align_bytes *= 2;
  return align_bytes * 8;
}

CompilerType TypeSystemCpp::GetBasicTypeFromAST(BasicType basic_type) {
  using cpp_typesystem::BuiltinKind;
  // Map the language-neutral BasicType onto one of our enumerated builtin
  // kinds. Only the kinds TypeSystemCpp models are listed; anything else has
  // no basic type here.
  std::optional<BuiltinKind> kind;
  switch (basic_type) {
  case eBasicTypeVoid:
    kind = BuiltinKind::Void;
    break;
  case eBasicTypeBool:
    kind = BuiltinKind::Bool;
    break;
  case eBasicTypeChar:
    kind = BuiltinKind::Char;
    break;
  case eBasicTypeSignedChar:
    kind = BuiltinKind::SignedChar;
    break;
  case eBasicTypeUnsignedChar:
    kind = BuiltinKind::UnsignedChar;
    break;
  case eBasicTypeWChar:
    kind = BuiltinKind::WCharT;
    break;
  case eBasicTypeChar8:
    kind = BuiltinKind::Char8;
    break;
  case eBasicTypeChar16:
    kind = BuiltinKind::Char16;
    break;
  case eBasicTypeChar32:
    kind = BuiltinKind::Char32;
    break;
  case eBasicTypeShort:
    kind = BuiltinKind::Short;
    break;
  case eBasicTypeUnsignedShort:
    kind = BuiltinKind::UnsignedShort;
    break;
  case eBasicTypeInt:
    kind = BuiltinKind::Int;
    break;
  case eBasicTypeUnsignedInt:
    kind = BuiltinKind::UnsignedInt;
    break;
  case eBasicTypeLong:
    kind = BuiltinKind::Long;
    break;
  case eBasicTypeUnsignedLong:
    kind = BuiltinKind::UnsignedLong;
    break;
  case eBasicTypeLongLong:
    kind = BuiltinKind::LongLong;
    break;
  case eBasicTypeUnsignedLongLong:
    kind = BuiltinKind::UnsignedLongLong;
    break;
  case eBasicTypeInt128:
    kind = BuiltinKind::Int128;
    break;
  case eBasicTypeUnsignedInt128:
    kind = BuiltinKind::UnsignedInt128;
    break;
  case eBasicTypeFloat:
    kind = BuiltinKind::Float;
    break;
  case eBasicTypeDouble:
    kind = BuiltinKind::Double;
    break;
  case eBasicTypeLongDouble:
    kind = BuiltinKind::LongDouble;
    break;
  default:
    break;
  }
  if (!kind)
    return CompilerType();
  return GetCompilerType(m_context.GetBuiltinType(*kind));
}

CompilerType
TypeSystemCpp::GetBuiltinTypeForEncodingAndBitSize(Encoding encoding,
                                                   size_t bit_size) {
  return CompilerType();
}

bool TypeSystemCpp::IsBeingDefined(opaque_compiler_type_t type) {
  return false;
}

bool TypeSystemCpp::IsConst(opaque_compiler_type_t type) {
  if (!type)
    return false;
  if (auto *cv =
          llvm::dyn_cast<cpp_typesystem::CVQualifiedType>(GetCppType(type)))
    return cv->IsConst();
  return false;
}

uint32_t TypeSystemCpp::IsHomogeneousAggregate(opaque_compiler_type_t type,
                                               CompilerType *base_type_ptr) {
  return 0;
}

bool TypeSystemCpp::IsPolymorphicClass(opaque_compiler_type_t type) {
  return false;
}

bool TypeSystemCpp::IsTypedefType(opaque_compiler_type_t type) {
  return type && llvm::isa<cpp_typesystem::TypedefType>(GetCppType(type));
}

CompilerType TypeSystemCpp::GetTypedefedType(opaque_compiler_type_t type) {
  if (!type)
    return CompilerType();
  if (auto *td = llvm::dyn_cast<cpp_typesystem::TypedefType>(GetCppType(type)))
    return GetCompilerType(td->GetUnderlyingType());
  return CompilerType();
}

bool TypeSystemCpp::IsVectorType(opaque_compiler_type_t type,
                                 CompilerType *element_type, uint64_t *size) {
  return false;
}

CompilerType
TypeSystemCpp::GetFullyUnqualifiedType(opaque_compiler_type_t type) {
  return CompilerType();
}

CompilerType TypeSystemCpp::GetNonReferenceType(opaque_compiler_type_t type) {
  if (!type)
    return CompilerType();
  // Peeling only the reference (not typedef/cv sugar) mirrors clang: e.g. the
  // non-reference type of `const int &` is `const int`.
  if (auto *ref =
          llvm::dyn_cast<cpp_typesystem::ReferenceType>(GetCppType(type)))
    return GetCompilerType(ref->GetPointeeType());
  return CompilerType(weak_from_this(), type);
}

bool TypeSystemCpp::IsReferenceType(opaque_compiler_type_t type,
                                    CompilerType *pointee_type,
                                    bool *is_rvalue) {
  if (pointee_type)
    pointee_type->Clear();
  if (is_rvalue)
    *is_rvalue = false;
  auto *ref = llvm::dyn_cast_or_null<cpp_typesystem::ReferenceType>(
      type ? Desugar(GetCppType(type)) : nullptr);
  if (!ref)
    return false;
  if (pointee_type)
    *pointee_type = GetCompilerType(ref->GetPointeeType());
  if (is_rvalue)
    *is_rvalue = ref->IsRValue();
  return true;
}

/// The record backing a class-template instantiation, or null if \p type is not
/// a (possibly sugared) record. Template arguments are populated during
/// completion, so callers must complete the type first.
static cpp_typesystem::RecordType *
GetRecordForTemplateArgs(cpp_typesystem::Type *type) {
  return llvm::dyn_cast_or_null<cpp_typesystem::RecordType>(Desugar(type));
}

bool TypeSystemCpp::IsTemplateType(opaque_compiler_type_t type) {
  if (!type)
    return false;
  GetCompleteType(type);
  if (auto *record = GetRecordForTemplateArgs(GetCppType(type)))
    return record->GetNumTemplateArguments() > 0;
  return false;
}

size_t TypeSystemCpp::GetNumTemplateArguments(opaque_compiler_type_t type,
                                              bool expand_pack) {
  if (!type)
    return 0;
  GetCompleteType(type);
  if (auto *record = GetRecordForTemplateArgs(GetCppType(type)))
    return record->GetNumTemplateArguments();
  return 0;
}

lldb::TemplateArgumentKind
TypeSystemCpp::GetTemplateArgumentKind(opaque_compiler_type_t type, size_t idx,
                                       bool expand_pack) {
  if (!type)
    return eTemplateArgumentKindNull;
  GetCompleteType(type);
  if (auto *record = GetRecordForTemplateArgs(GetCppType(type)))
    if (const cpp_typesystem::TemplateArgument *arg =
            record->GetTemplateArgumentAtIndex(idx))
      return arg->kind;
  return eTemplateArgumentKindNull;
}

CompilerType TypeSystemCpp::GetTypeTemplateArgument(opaque_compiler_type_t type,
                                                    size_t idx,
                                                    bool expand_pack) {
  if (!type)
    return CompilerType();
  GetCompleteType(type);
  if (auto *record = GetRecordForTemplateArgs(GetCppType(type)))
    if (const cpp_typesystem::TemplateArgument *arg =
            record->GetTemplateArgumentAtIndex(idx))
      return GetCompilerType(arg->type.Get());
  return CompilerType();
}

std::optional<CompilerType::IntegralTemplateArgument>
TypeSystemCpp::GetIntegralTemplateArgument(opaque_compiler_type_t type,
                                           size_t idx, bool expand_pack) {
  if (!type)
    return std::nullopt;
  GetCompleteType(type);
  auto *record = GetRecordForTemplateArgs(GetCppType(type));
  if (!record)
    return std::nullopt;
  const cpp_typesystem::TemplateArgument *arg =
      record->GetTemplateArgumentAtIndex(idx);
  if (!arg || arg->kind != eTemplateArgumentKindIntegral)
    return std::nullopt;

  // Reconstruct the value with the argument type's signedness.
  Scalar value;
  if (arg->type && arg->type.Get()->GetEncoding() == eEncodingSint)
    value = static_cast<int64_t>(arg->integral_value);
  else
    value = arg->integral_value;
  return CompilerType::IntegralTemplateArgument{value,
                                                GetCompilerType(arg->type.Get())};
}

CompilerType
TypeSystemCpp::GetDirectNestedTypeWithName(opaque_compiler_type_t type,
                                           llvm::StringRef name) {
  if (!type)
    return CompilerType();
  GetCompleteType(type);
  if (auto *record = GetRecordForTemplateArgs(GetCppType(type)))
    if (cpp_typesystem::Type *nested = record->GetNestedTypeWithName(name))
      return GetCompilerType(nested);
  return CompilerType();
}
