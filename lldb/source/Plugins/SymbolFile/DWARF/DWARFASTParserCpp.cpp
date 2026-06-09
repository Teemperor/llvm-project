//===-- DWARFASTParserCpp.cpp - DWARF AST parser for TypeSystemCpp --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DWARFASTParserCpp.h"

#include "DWARFDeclContext.h"
#include "DWARFDIE.h"
#include "DWARFDebugInfo.h"
#include "DWARFAttribute.h"
#include "DWARFFormValue.h"
#include "SymbolFileDWARF.h"
#include "SymbolFileDWARFDebugMap.h"

#include "Plugins/TypeSystem/Clang/TypeSystemCpp.h"

#include "lldb/Core/Module.h"
#include "lldb/Symbol/CompilerDecl.h"
#include "lldb/Symbol/CompilerDeclContext.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Expression/Expression.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Target/Language.h"
#include "llvm/BinaryFormat/Dwarf.h"

using namespace lldb_private;
using namespace lldb_private::plugin::dwarf;
using namespace llvm::dwarf;

static std::string MakeLLDBFuncAsmLabel(const DWARFDIE &die) {
  const char *name = die.GetMangledName(/*substitute_name_allowed=*/false);
  if (!name)
    return {};
  SymbolFileDWARF *dwarf = die.GetDWARF();
  if (!dwarf)
    return {};
  auto get_module_id = [&](SymbolFile *sym) -> lldb::user_id_t {
    if (!sym)
      return LLDB_INVALID_UID;
    auto *obj = sym->GetMainObjectFile();
    if (!obj)
      return LLDB_INVALID_UID;
    auto module_sp = obj->GetModule();
    if (!module_sp)
      return LLDB_INVALID_UID;
    return module_sp->GetID();
  };
  lldb::user_id_t module_id = get_module_id(dwarf->GetDebugMapSymfile());
  if (module_id == LLDB_INVALID_UID)
    module_id = get_module_id(dwarf);
  if (module_id == LLDB_INVALID_UID)
    return {};
  const auto die_id = die.GetID();
  if (die_id == LLDB_INVALID_UID)
    return {};
  return FunctionCallLabel{/*discriminator=*/{}, /*module_id=*/module_id,
                           /*symbol_id=*/die_id, /*.lookup_name=*/name}
      .toString();
}

DWARFASTParserCpp::DWARFASTParserCpp(TypeSystemCpp &ast)
    : DWARFASTParser(Kind::DWARFASTParserCpp), m_ast(ast) {}

// Look up the enumerator name for a given value in an enum DIE.
static std::string GetEnumeratorName(DWARFDIE enum_die, int64_t value) {
  for (DWARFDIE child : enum_die.children()) {
    if (child.Tag() != DW_TAG_enumerator)
      continue;
    DWARFAttributes attrs = child.GetAttributes();
    for (size_t i = 0; i < attrs.Size(); ++i) {
      if (attrs.AttributeAtIndex(i) == DW_AT_const_value) {
        DWARFFormValue fv;
        if (attrs.ExtractFormValueAtIndex(i, fv) && fv.Signed() == value) {
          if (const char *n = child.GetName())
            return n;
        }
      }
    }
  }
  return {};
}

// Forward declaration
static std::string GetDIETemplateParams(DWARFDIE die);
static std::string GetDIEScopePrefix(DWARFDIE die);

// Append a single template type argument's name to os.
static void AppendTemplateTypeArg(llvm::raw_string_ostream &os, DWARFDIE type_die) {
  if (!type_die)
    return;
  // Handle const/volatile wrappers
  if (type_die.Tag() == DW_TAG_const_type || type_die.Tag() == DW_TAG_volatile_type) {
    if (type_die.Tag() == DW_TAG_const_type) os << "const ";
    else os << "volatile ";
    AppendTemplateTypeArg(os, type_die.GetReferencedDIE(DW_AT_type));
    return;
  }
  if (type_die.Tag() == DW_TAG_pointer_type) {
    AppendTemplateTypeArg(os, type_die.GetReferencedDIE(DW_AT_type));
    os << "*";
    return;
  }
  if (type_die.Tag() == DW_TAG_reference_type) {
    AppendTemplateTypeArg(os, type_die.GetReferencedDIE(DW_AT_type));
    os << "&";
    return;
  }
  if (const char *n = type_die.GetName()) {
    // Include scope prefix (e.g., "ns::") for namespace-qualified types.
    os << GetDIEScopePrefix(type_die);
    os << n;
    // Recursively append template args for template instantiation type args.
    std::string tmpl = GetDIETemplateParams(type_die);
    os << tmpl;
  }
}

// Returns the full template argument suffix for a DIE (e.g. "<int, float>"),
// or empty string if the DIE is not a template or already has args in its name.
static std::string GetDIETemplateParams(DWARFDIE die) {
  if (!die)
    return {};
  if (DWARFDIE sig_die = die.GetReferencedDIE(DW_AT_signature))
    die = sig_die;
  const char *name = die.GetName();
  if (!name || llvm::StringRef(name).contains('<'))
    return {};

  std::string result;
  llvm::raw_string_ostream os(result);
  bool first = true;

  for (DWARFDIE child : die.children()) {
    dw_tag_t tag = child.Tag();
    if (tag == DW_TAG_template_type_parameter) {
      if (first) os << "<"; else os << ", ";
      first = false;
      DWARFDIE type_die = child.GetReferencedDIE(DW_AT_type);
      AppendTemplateTypeArg(os, type_die);
    } else if (tag == DW_TAG_template_value_parameter) {
      if (first) os << "<"; else os << ", ";
      first = false;
      DWARFDIE type_die = child.GetReferencedDIE(DW_AT_type);
      DWARFAttributes attrs = child.GetAttributes();
      int64_t val = 0;
      bool val_valid = false;
      for (size_t i = 0; i < attrs.Size(); ++i) {
        if (attrs.AttributeAtIndex(i) == DW_AT_const_value) {
          DWARFFormValue fv;
          if (attrs.ExtractFormValueAtIndex(i, fv)) {
            val = fv.Signed();
            val_valid = true;
          }
        }
      }
      if (type_die && type_die.Tag() == DW_TAG_enumeration_type) {
        // For enum template params, resolve to enumerator name.
        const char *enum_name = type_die.GetName();
        std::string enumerator = val_valid ? GetEnumeratorName(type_die, val) : "";
        if (!enumerator.empty()) {
          if (enum_name) { os << enum_name; os << "::"; }
          os << enumerator;
        } else if (enum_name) {
          os << "(" << enum_name << ")" << val;
        } else {
          os << val;
        }
      } else if (type_die) {
        const char *type_name = type_die.GetName();
        if (type_name && llvm::StringRef(type_name) == "bool")
          os << (val ? "true" : "false");
        else
          os << val;
      } else {
        os << val;
      }
    } else if (tag == DW_TAG_GNU_template_template_param) {
      if (first) os << "<"; else os << ", ";
      first = false;
      DWARFAttributes attrs = child.GetAttributes();
      for (size_t i = 0; i < attrs.Size(); ++i) {
        if (attrs.AttributeAtIndex(i) == DW_AT_GNU_template_name) {
          DWARFFormValue fv;
          if (attrs.ExtractFormValueAtIndex(i, fv))
            if (const char *n = fv.AsCString())
              os << n;
        }
      }
    } else if (tag == DW_TAG_GNU_template_parameter_pack) {
      // Pack: iterate its children as individual template args.
      for (DWARFDIE pack_child : child.children()) {
        dw_tag_t pack_tag = pack_child.Tag();
        if (pack_tag == DW_TAG_GNU_template_template_param) {
          if (first) os << "<"; else os << ", ";
          first = false;
          DWARFAttributes attrs = pack_child.GetAttributes();
          for (size_t i = 0; i < attrs.Size(); ++i) {
            if (attrs.AttributeAtIndex(i) == DW_AT_GNU_template_name) {
              DWARFFormValue fv;
              if (attrs.ExtractFormValueAtIndex(i, fv))
                if (const char *n = fv.AsCString())
                  os << n;
            }
          }
        } else if (pack_tag == DW_TAG_template_type_parameter) {
          if (first) os << "<"; else os << ", ";
          first = false;
          AppendTemplateTypeArg(os, pack_child.GetReferencedDIE(DW_AT_type));
        }
      }
    }
  }

  if (!first) {
    // Close template argument list
    if (!result.empty() && result.back() == '>')
      os << " >";
    else
      os << ">";
  }
  return result;
}

// ---- Namespace helpers -------------------------------------------------------

// Build the scope prefix for a DIE (e.g. "ns::" or "Foo<int>::"), by walking
// up the parent DIE chain. Does NOT include the name of the DIE itself.
static std::string GetDIEScopePrefix(DWARFDIE die) {
  std::string prefix;
  DWARFDIE parent = die.GetParent();
  while (parent) {
    dw_tag_t tag = parent.Tag();
    if (tag == DW_TAG_namespace) {
      const char *ns_name = parent.GetName();
      if (ns_name)
        prefix = std::string(ns_name) + "::" + prefix;
      // anonymous namespaces: skip (no name)
    } else if (tag == DW_TAG_structure_type || tag == DW_TAG_class_type ||
               tag == DW_TAG_union_type) {
      const char *cls_name = parent.GetName();
      if (cls_name) {
        std::string cls = cls_name;
        cls += GetDIETemplateParams(parent);
        prefix = cls + "::" + prefix;
      }
    } else if (tag == DW_TAG_compile_unit || tag == DW_TAG_partial_unit) {
      break;
    }
    parent = parent.GetParent();
  }
  return prefix;
}

LLDBNamespaceNode *
DWARFASTParserCpp::ResolveNamespaceDIE(const DWARFDIE &die) {
  if (!die || die.Tag() != DW_TAG_namespace)
    return nullptr;

  uint64_t offset = die.GetOffset();
  auto it = m_die_to_namespace.find(offset);
  if (it != m_die_to_namespace.end())
    return it->second;

  const char *name_cstr = die.GetName();
  std::string name = name_cstr ? name_cstr : "";

  LLDBNamespaceNode *parent = GetParentNamespace(die);

  std::string qualified;
  if (parent && !parent->qualified_name.empty())
    qualified = parent->qualified_name + "::" + name;
  else
    qualified = name;

  // Check for DW_AT_export_symbols (inline namespace)
  bool is_inline = false;
  DWARFAttributes attrs = die.GetAttributes();
  for (size_t i = 0; i < attrs.Size(); ++i) {
    if (attrs.AttributeAtIndex(i) == DW_AT_export_symbols) {
      DWARFFormValue fv;
      if (attrs.ExtractFormValueAtIndex(i, fv))
        is_inline = fv.Boolean();
      break;
    }
  }

  LLDBNamespaceNode *ns = m_ast.GetTypeRegistry().GetOrCreateNamespace(
      qualified, parent, is_inline);
  m_die_to_namespace[offset] = ns;
  return ns;
}

LLDBNamespaceNode *
DWARFASTParserCpp::GetParentNamespace(const DWARFDIE &die) {
  DWARFDIE parent = die.GetParent();
  if (!parent)
    return nullptr;
  if (parent.Tag() == DW_TAG_namespace)
    return ResolveNamespaceDIE(parent);
  return nullptr; // TU-level
}

LLDBNamespaceNode *
DWARFASTParserCpp::GetNamespaceForDIE(const DWARFDIE &die) {
  // Walk up the die hierarchy to find the enclosing namespace
  DWARFDIE parent = die.GetParent();
  while (parent) {
    if (parent.Tag() == DW_TAG_namespace)
      return ResolveNamespaceDIE(parent);
    if (parent.Tag() == DW_TAG_compile_unit ||
        parent.Tag() == DW_TAG_partial_unit)
      return nullptr; // TU level
    parent = parent.GetParent();
  }
  return nullptr;
}

CompilerDeclContext
DWARFASTParserCpp::MakeNamespaceContext(LLDBNamespaceNode *ns) {
  if (!ns)
    return CompilerDeclContext(&m_ast, nullptr); // TU-level
  return CompilerDeclContext(&m_ast, ns);
}

// ---- Type helpers ------------------------------------------------------------

LLDBQualType DWARFASTParserCpp::GetQualTypeForDIE(const DWARFDIE &die) {
  if (!die)
    return LLDBQualType();
  return LLDBQualType(GetOrParseType(die));
}

LLDBQualType DWARFASTParserCpp::GetOrParseQualType(const DWARFDIE &die) {
  if (!die)
    return {};
  LLDBQualifiers quals;
  DWARFDIE d = die;
  while (d) {
    switch (d.Tag()) {
    case DW_TAG_const_type:
      quals.is_const = true;
      d = d.GetReferencedDIE(DW_AT_type);
      continue;
    case DW_TAG_volatile_type:
      quals.is_volatile = true;
      d = d.GetReferencedDIE(DW_AT_type);
      continue;
    case DW_TAG_restrict_type:
      quals.is_restrict = true;
      d = d.GetReferencedDIE(DW_AT_type);
      continue;
    default:
      break;
    }
    break;
  }
  if (!d)
    return {};
  return LLDBQualType(GetOrParseType(d), quals);
}

LLDBTypeNode *DWARFASTParserCpp::GetOrParseType(const DWARFDIE &die) {
  if (!die)
    return nullptr;
  uint64_t offset = die.GetOffset();
  auto it = m_die_to_type.find(offset);
  if (it != m_die_to_type.end())
    return it->second;

  // Avoid infinite recursion for cycles (can happen with struct pointers)
  m_die_to_type[offset] = nullptr; // sentinel

  LLDBTypeNode *node = nullptr;

  switch (die.Tag()) {
  case DW_TAG_base_type: {
    uint64_t byte_size = 0;
    uint8_t encoding = 0;
    llvm::StringRef type_name;
    DWARFAttributes attrs = die.GetAttributes();
    for (size_t i = 0; i < attrs.Size(); ++i) {
      DWARFFormValue fv;
      switch (attrs.AttributeAtIndex(i)) {
      case DW_AT_byte_size:
        if (attrs.ExtractFormValueAtIndex(i, fv))
          byte_size = fv.Unsigned();
        break;
      case DW_AT_encoding:
        if (attrs.ExtractFormValueAtIndex(i, fv))
          encoding = (uint8_t)fv.Unsigned();
        break;
      case DW_AT_name:
        if (attrs.ExtractFormValueAtIndex(i, fv))
          if (const char *s = fv.AsCString())
            type_name = s;
        break;
      default:
        break;
      }
    }
    uint32_t bits = (uint32_t)(byte_size * 8);
    // Recognize wchar_t / char8_t / char16_t / char32_t by type_name first,
    // since their DW_ATE encoding overlaps with integer types.
    if (type_name == "wchar_t") {
      node = m_ast.GetTypeRegistry().GetOrCreateBuiltin(
          lldb::eBasicTypeWChar, bits, /*is_signed=*/false, /*is_float=*/false);
      break;
    }
    if (type_name == "char8_t") {
      node = m_ast.GetTypeRegistry().GetOrCreateBuiltin(
          lldb::eBasicTypeChar8, bits, false, false);
      break;
    }
    if (type_name == "char16_t") {
      node = m_ast.GetTypeRegistry().GetOrCreateBuiltin(
          lldb::eBasicTypeChar16, bits, false, false);
      break;
    }
    if (type_name == "char32_t") {
      node = m_ast.GetTypeRegistry().GetOrCreateBuiltin(
          lldb::eBasicTypeChar32, bits, false, false);
      break;
    }
    // Map DW_ATE_* to BasicType
    lldb::BasicType bt = lldb::eBasicTypeInvalid;
    bool is_signed = false, is_float = false;
    switch (encoding) {
    case DW_ATE_boolean:
      bt = lldb::eBasicTypeBool;
      break;
    case DW_ATE_float:
      is_float = true;
      if (type_name == "long double")
        bt = lldb::eBasicTypeLongDouble;
      else if (bits <= 16)
        bt = lldb::eBasicTypeHalf;
      else if (bits <= 32)
        bt = lldb::eBasicTypeFloat;
      else if (bits <= 64)
        bt = lldb::eBasicTypeDouble;
      else
        bt = lldb::eBasicTypeLongDouble;
      break;
    case DW_ATE_signed:
      is_signed = true;
      if (bits <= 8)
        bt = lldb::eBasicTypeSignedChar;
      else if (bits <= 16)
        bt = lldb::eBasicTypeShort;
      else if (bits <= 32)
        bt = lldb::eBasicTypeInt;
      else if (type_name == "long long" || type_name == "__int64")
        bt = lldb::eBasicTypeLongLong;
      else
        bt = lldb::eBasicTypeLong;
      break;
    case DW_ATE_signed_char:
      is_signed = true;
      bt = (type_name == "char") ? lldb::eBasicTypeChar : lldb::eBasicTypeSignedChar;
      break;
    case DW_ATE_unsigned:
      if (bits <= 8)
        bt = lldb::eBasicTypeUnsignedChar;
      else if (bits <= 16)
        bt = lldb::eBasicTypeUnsignedShort;
      else if (bits <= 32)
        bt = lldb::eBasicTypeUnsignedInt;
      else if (type_name == "unsigned long long" || type_name == "unsigned __int64")
        bt = lldb::eBasicTypeUnsignedLongLong;
      else
        bt = lldb::eBasicTypeUnsignedLong;
      break;
    case DW_ATE_unsigned_char:
      bt = lldb::eBasicTypeUnsignedChar;
      break;
    case DW_ATE_UTF:
      bt = (bits <= 8) ? lldb::eBasicTypeChar8
                       : (bits <= 16) ? lldb::eBasicTypeChar16
                                      : lldb::eBasicTypeChar32;
      break;
    case DW_ATE_complex_float: {
      // _Complex float / _Complex double / _Complex long double
      uint32_t elem_bits = bits / 2;
      lldb::BasicType elem_bt = lldb::eBasicTypeFloat;
      if (elem_bits <= 32)
        elem_bt = lldb::eBasicTypeFloat;
      else if (elem_bits <= 64)
        elem_bt = lldb::eBasicTypeDouble;
      else
        elem_bt = lldb::eBasicTypeLongDouble;
      LLDBTypeNode *elem = m_ast.GetTypeRegistry().GetOrCreateBuiltin(
          elem_bt, elem_bits, false, true);
      node = m_ast.GetTypeRegistry().CreateComplexType(elem);
      break;
    }
    case DW_ATE_lo_user:
      // DW_ATE_lo_user is used by Apple/Clang for _Complex integer types.
      if (type_name.contains("complex")) {
        uint32_t elem_bits = bits / 2;
        lldb::BasicType elem_bt = lldb::eBasicTypeInt;
        bool elem_signed = true;
        if (elem_bits <= 8)        elem_bt = lldb::eBasicTypeSignedChar;
        else if (elem_bits <= 16)  elem_bt = lldb::eBasicTypeShort;
        else if (elem_bits <= 32)  elem_bt = lldb::eBasicTypeInt;
        else if (type_name.contains("long long") || type_name.contains("__int64"))
          elem_bt = lldb::eBasicTypeLongLong;
        else
          elem_bt = lldb::eBasicTypeLong;
        LLDBTypeNode *elem = m_ast.GetTypeRegistry().GetOrCreateBuiltin(
            elem_bt, elem_bits, elem_signed, false);
        node = m_ast.GetTypeRegistry().CreateComplexType(elem);
        break;
      }
      bt = lldb::eBasicTypeVoid;
      break;
    default:
      bt = lldb::eBasicTypeVoid;
      break;
    }
    if (node)
      break;
    if (bt == lldb::eBasicTypeInvalid)
      bt = lldb::eBasicTypeVoid;
    node = m_ast.GetTypeRegistry().GetOrCreateBuiltin(bt, bits, is_signed,
                                                      is_float);
    break;
  }

  case DW_TAG_pointer_type: {
    DWARFDIE pointee_die = die.GetReferencedDIE(DW_AT_type);
    // Check for Clang blocks: a pointer to a struct with DW_AT_APPLE_block has
    // a __FuncPtr member whose pointee is the block's function type.
    if (pointee_die &&
        pointee_die.GetAttributeValueAsUnsigned(DW_AT_APPLE_block, 0)) {
      for (DWARFDIE child_die : pointee_die.children()) {
        if (llvm::StringRef(
                child_die.GetAttributeValueAsString(DW_AT_name, "")) ==
            "__FuncPtr") {
          DWARFDIE fn_ptr_die = child_die.GetReferencedDIE(DW_AT_type);
          if (fn_ptr_die) {
            DWARFDIE fn_die = fn_ptr_die.GetReferencedDIE(DW_AT_type);
            if (fn_die) {
              LLDBTypeNode *fn_node = GetOrParseType(fn_die);
              if (fn_node) {
                node = m_ast.GetTypeRegistry().CreateBlockPointerType(
                    LLDBQualType(fn_node));
                break;
              }
            }
          }
          break;
        }
      }
    }
    if (!node) {
      LLDBQualType pointee;
      if (pointee_die)
        pointee = GetOrParseQualType(pointee_die);
      // void pointer if no type attribute
      node = m_ast.GetTypeRegistry().CreatePointerType(pointee);
    }
    break;
  }

  case DW_TAG_reference_type: {
    DWARFDIE pointee_die = die.GetReferencedDIE(DW_AT_type);
    LLDBQualType pointee;
    if (pointee_die)
      pointee = GetOrParseQualType(pointee_die);
    node = m_ast.GetTypeRegistry().CreateLValueRefType(pointee);
    break;
  }

  case DW_TAG_rvalue_reference_type: {
    DWARFDIE pointee_die = die.GetReferencedDIE(DW_AT_type);
    LLDBQualType pointee;
    if (pointee_die)
      pointee = GetOrParseQualType(pointee_die);
    node = m_ast.GetTypeRegistry().CreateRValueRefType(pointee);
    break;
  }

  case DW_TAG_typedef:
  case DW_TAG_template_alias: {
    const char *name = die.GetName();
    DWARFDIE underlying_die = die.GetReferencedDIE(DW_AT_type);
    LLDBQualType underlying;
    if (underlying_die)
      underlying = LLDBQualType(GetOrParseType(underlying_die));
    node = m_ast.GetTypeRegistry().CreateTypedef(name ? name : "", underlying);
    auto *td = node->As<LLDBTypedefTypeNode>();
    if (name) {
      std::string prefix = GetDIEScopePrefix(die);
      td->qualified_name = prefix.empty() ? name : prefix + name;
    }
    // Track parent class so ClangASTGenerator can create the typedef inside it.
    DWARFDIE parent_die = die.GetParent();
    if (parent_die && (parent_die.Tag() == DW_TAG_structure_type ||
                       parent_die.Tag() == DW_TAG_class_type ||
                       parent_die.Tag() == DW_TAG_union_type)) {
      auto *parent_node = GetOrParseType(parent_die);
      td->parent_node = parent_node;
      // Also register this typedef in the parent record so that
      // ClangASTGenerator can add it during record completion.
      if (parent_node && parent_node->kind == TypeNodeKind::Record)
        parent_node->As<LLDBRecordTypeNode>()->nested_typedefs.push_back(td);
    }
    break;
  }

  case DW_TAG_const_type: {
    DWARFDIE base_die = die.GetReferencedDIE(DW_AT_type);
    if (base_die)
      node = GetOrParseType(base_die);
    break;
  }

  case DW_TAG_volatile_type:
  case DW_TAG_restrict_type: {
    DWARFDIE base_die = die.GetReferencedDIE(DW_AT_type);
    if (base_die)
      node = GetOrParseType(base_die);
    break;
  }

  case DW_TAG_atomic_type: {
    DWARFDIE base_die = die.GetReferencedDIE(DW_AT_type);
    LLDBQualType vt;
    if (base_die)
      vt = LLDBQualType(GetOrParseType(base_die));
    node = m_ast.GetTypeRegistry().CreateAtomicType(vt);
    break;
  }

  case DW_TAG_LLVM_ptrauth_type: {
    DWARFDIE base_die = die.GetReferencedDIE(DW_AT_type);
    LLDBTypeNode *underlying = base_die ? GetOrParseType(base_die) : nullptr;
    if (!underlying)
      break;
    // Build the ptrauth-qualified type name.
    // Format depends on whether underlying is a pointer or a named type:
    //   pointer: "int *__ptrauth(key,addr,extra)"
    //   named:   "__ptrauth(key,addr,extra) intp"
    unsigned key = die.GetAttributeValueAsUnsigned(DW_AT_LLVM_ptrauth_key, 0);
    unsigned addr_disc = die.GetAttributeValueAsUnsigned(
        DW_AT_LLVM_ptrauth_address_discriminated, 0);
    unsigned extra = die.GetAttributeValueAsUnsigned(
        DW_AT_LLVM_ptrauth_extra_discriminator, 0);
    std::string qualifier = "__ptrauth(" + std::to_string(key) + "," +
                            std::to_string(addr_disc) + "," +
                            std::to_string(extra) + ")";
    CompilerType underlying_ct = m_ast.MakeCompilerType(LLDBQualType(underlying));
    std::string base_name = underlying_ct.GetTypeName().GetStringRef().str();
    std::string ptrauth_name;
    if (underlying->kind == TypeNodeKind::Pointer)
      ptrauth_name = base_name + qualifier;
    else
      ptrauth_name = qualifier + " " + base_name;
    auto *td = m_ast.GetTypeRegistry().CreateTypedef(ptrauth_name, LLDBQualType(underlying));
    td->qualified_name = ptrauth_name;
    td->is_ptrauth = true;
    node = td;
    break;
  }

  case DW_TAG_array_type: {
    DWARFDIE elem_die = die.GetReferencedDIE(DW_AT_type);
    LLDBQualType elem;
    if (elem_die)
      elem = LLDBQualType(GetOrParseType(elem_die));
    // Collect all subrange dimensions (outermost first in DWARF)
    std::vector<std::optional<uint64_t>> dims;
    for (DWARFDIE child : die.children()) {
      if (child.Tag() == DW_TAG_subrange_type) {
        std::optional<uint64_t> count;
        DWARFAttributes sub_attrs = child.GetAttributes();
        uint64_t lower_bound = 0;
        uint64_t upper_bound = 0;
        bool upper_bound_valid = false;
        for (size_t i = 0; i < sub_attrs.Size(); ++i) {
          DWARFFormValue fv;
          dw_attr_t attr = sub_attrs.AttributeAtIndex(i);
          if (!sub_attrs.ExtractFormValueAtIndex(i, fv))
            continue;
          if (attr == DW_AT_count) {
            // For VLAs, DW_AT_count references a variable DIE; use nullopt.
            if (!child.GetReferencedDIE(DW_AT_count))
              count = fv.Unsigned();
          } else if (attr == DW_AT_upper_bound) {
            upper_bound_valid = true;
            upper_bound = fv.Unsigned();
          } else if (attr == DW_AT_lower_bound) {
            lower_bound = fv.Unsigned();
          }
        }
        if (!count && upper_bound_valid && upper_bound >= lower_bound)
          count = upper_bound - lower_bound + 1;
        dims.push_back(count);
      }
    }
    if (dims.empty()) {
      auto *arr_node = m_ast.GetTypeRegistry().CreateArrayType(elem, std::nullopt);
      // Record the DWARF UID for VLA runtime-size lookup.
      if (auto *arr = arr_node->As<LLDBArrayTypeNode>())
        arr->vla_dwarf_uid = die.GetID();
      node = arr_node;
    } else {
      // Build nested array types from innermost to outermost dimension.
      // DWARF lists dimensions outermost-first, so reverse to build innermost first.
      LLDBQualType cur = elem;
      for (int i = (int)dims.size() - 1; i >= 0; --i) {
        auto *arr_node = m_ast.GetTypeRegistry().CreateArrayType(cur, dims[i]);
        // For VLA dimensions (nullopt), record the DWARF UID.
        if (!dims[i]) {
          if (auto *arr = arr_node->As<LLDBArrayTypeNode>())
            arr->vla_dwarf_uid = die.GetID();
        }
        cur = LLDBQualType(arr_node);
      }
      node = cur.node;
    }
    break;
  }

  case DW_TAG_ptr_to_member_type: {
    DWARFDIE cls_die = die.GetReferencedDIE(DW_AT_containing_type);
    DWARFDIE pt_die = die.GetReferencedDIE(DW_AT_type);
    LLDBQualType cls, pt;
    if (cls_die)
      cls = LLDBQualType(GetOrParseType(cls_die));
    if (pt_die)
      pt = LLDBQualType(GetOrParseType(pt_die));
    node = m_ast.GetTypeRegistry().CreateMemberPointerType(cls, pt);
    break;
  }

  case DW_TAG_structure_type:
  case DW_TAG_class_type:
  case DW_TAG_union_type: {
    const char *name = die.GetName();
    bool is_union = (die.Tag() == DW_TAG_union_type);
    bool is_class = (die.Tag() == DW_TAG_class_type);
    std::string full_name = name ? name : "";
    full_name += GetDIETemplateParams(die);
    auto *rec = m_ast.GetTypeRegistry().CreateRecordType(
        full_name, is_union, is_class);
    rec->qualified_name = GetDIEScopePrefix(die) + full_name;
    // If this record is lexically nested inside another record, track the
    // parent so CppASTTranslator can set the correct Clang DeclContext.
    {
      DWARFDIE parent_die = die.GetParent();
      if (parent_die && (parent_die.Tag() == DW_TAG_structure_type ||
                         parent_die.Tag() == DW_TAG_class_type ||
                         parent_die.Tag() == DW_TAG_union_type)) {
        auto *parent_node = GetOrParseType(parent_die);
        if (parent_node && parent_node->kind == TypeNodeKind::Record) {
          rec->parent_record_node = parent_node->As<LLDBRecordTypeNode>();
          rec->parent_record_node->nested_records.push_back(rec);
        }
      } else if (parent_die && parent_die.Tag() == DW_TAG_namespace) {
        rec->parent_namespace_node = ResolveNamespaceDIE(parent_die);
      }
    }
    // Register early to break cycles
    m_die_to_type[offset] = rec;
    // Parse size and alignment
    DWARFAttributes attrs = die.GetAttributes();
    for (size_t i = 0; i < attrs.Size(); ++i) {
      DWARFFormValue fv;
      dw_attr_t attr = attrs.AttributeAtIndex(i);
      if (!attrs.ExtractFormValueAtIndex(i, fv))
        continue;
      if (attr == DW_AT_byte_size)
        rec->byte_size = fv.Unsigned();
      else if (attr == DW_AT_alignment)
        rec->alignment_bytes = fv.Unsigned();
    }
    // Children (fields, bases) parsed lazily in CompleteTypeFromDWARF.
    // Register in the forward-declaration map so CompleteType can find us.
    if (SymbolFileDWARF *dwarf = die.GetDWARF()) {
      if (auto die_ref = die.GetDIERef())
        dwarf->GetForwardDeclCompilerTypeToDIE()
            .try_emplace((void *)rec, *die_ref);
    }
    node = rec;
    break;
  }

  case DW_TAG_enumeration_type: {
    const char *name = die.GetName();
    DWARFDIE int_die = die.GetReferencedDIE(DW_AT_type);
    LLDBQualType int_type;
    if (int_die)
      int_type = LLDBQualType(GetOrParseType(int_die));
    else {
      // default: signed int
      int_type = LLDBQualType(m_ast.GetTypeRegistry().GetOrCreateBuiltin(
          lldb::eBasicTypeInt, 32, true, false));
    }
    bool is_scoped = false;
    DWARFAttributes attrs = die.GetAttributes();
    for (size_t i = 0; i < attrs.Size(); ++i) {
      DWARFFormValue fv;
      if (attrs.AttributeAtIndex(i) == DW_AT_enum_class &&
          attrs.ExtractFormValueAtIndex(i, fv))
        is_scoped = fv.Boolean();
    }
    std::string full_enum_name = name ? name : "";
    full_enum_name += GetDIETemplateParams(die);
    auto *en = m_ast.GetTypeRegistry().CreateEnumType(full_enum_name,
                                                      int_type, is_scoped);
    m_die_to_type[offset] = en;
    // Parse enumerators
    bool enum_is_signed = true;
    if (int_type.node && int_type.node->kind == TypeNodeKind::Builtin) {
      enum_is_signed = int_type.node->As<LLDBBuiltinTypeNode>()->is_signed;
    }
    for (DWARFDIE child : die.children()) {
      if (child.Tag() != DW_TAG_enumerator)
        continue;
      const char *en_name = child.GetName();
      DWARFAttributes en_attrs = child.GetAttributes();
      int64_t raw_val = 0;
      for (size_t i = 0; i < en_attrs.Size(); ++i) {
        DWARFFormValue fv;
        if (en_attrs.AttributeAtIndex(i) == DW_AT_const_value &&
            en_attrs.ExtractFormValueAtIndex(i, fv)) {
          raw_val = fv.Signed();
        }
      }
      llvm::APSInt val(llvm::APInt(64, (uint64_t)raw_val, enum_is_signed),
                       !enum_is_signed);
      en->enumerators.push_back({en_name ? en_name : "", val});
    }
    en->is_complete = true;
    node = en;
    break;
  }

  case DW_TAG_subprogram:
  case DW_TAG_subroutine_type: {
    DWARFDIE ret_die = die.GetReferencedDIE(DW_AT_type);
    LLDBQualType ret_type;
    if (ret_die)
      ret_type = GetOrParseQualType(ret_die);
    else
      ret_type = LLDBQualType(m_ast.GetTypeRegistry().GetOrCreateBuiltin(
          lldb::eBasicTypeVoid, 0, false, false));
    std::vector<LLDBParamNode> params;
    bool is_variadic = false;
    for (DWARFDIE child : die.children()) {
      if (child.Tag() == DW_TAG_formal_parameter) {
        DWARFDIE pt_die = child.GetReferencedDIE(DW_AT_type);
        LLDBQualType pt;
        if (pt_die)
          pt = GetOrParseQualType(pt_die);
        const char *pn = child.GetName();
        params.push_back({pn ? pn : "", pt});
      } else if (child.Tag() == DW_TAG_unspecified_parameters) {
        is_variadic = true;
      }
    }
    node = m_ast.GetTypeRegistry().CreateFunctionType(ret_type, std::move(params),
                                                      is_variadic);
    break;
  }

  case DW_TAG_unspecified_type: {
    // e.g. "decltype(nullptr)" or void
    node = m_ast.GetTypeRegistry().GetOrCreateBuiltin(lldb::eBasicTypeVoid, 0,
                                                      false, false);
    break;
  }

  default:
    break;
  }

  if (node)
    m_die_to_type[offset] = node;
  return node;
}

// ---- DWARFASTParser interface ------------------------------------------------

lldb::TypeSP DWARFASTParserCpp::ParseTypeFromDWARF(const SymbolContext &sc,
                                                    const DWARFDIE &die,
                                                    bool *type_is_new_ptr) {
  if (!die)
    return {};
  uint64_t offset = die.GetOffset();
  auto it = m_die_to_type_sp.find(offset);
  if (it != m_die_to_type_sp.end()) {
    if (type_is_new_ptr)
      *type_is_new_ptr = false;
    return it->second;
  }

  LLDBQualType qt = GetOrParseQualType(die);
  if (!qt.node)
    return {};

  CompilerType ct(m_ast.weak_from_this(),
                  (lldb::opaque_compiler_type_t)qt.node);
  if (qt.quals.is_const)
    ct = m_ast.AddConstModifier(ct.GetOpaqueQualType());
  if (qt.quals.is_volatile)
    ct = m_ast.AddVolatileModifier(ct.GetOpaqueQualType());
  if (qt.quals.is_restrict)
    ct = m_ast.AddRestrictModifier(ct.GetOpaqueQualType());
  LLDBTypeNode *node = qt.node;

  SymbolFileDWARF *dwarf = die.GetDWARF();
  if (!dwarf)
    return {};

  // Use Forward resolve state for Records/Enums so that CompleteType is
  // called lazily when fields are needed.
  bool needs_completion =
      (node->kind == TypeNodeKind::Record &&
       !node->As<LLDBRecordTypeNode>()->is_complete) ||
      (node->kind == TypeNodeKind::Enum &&
       !node->As<LLDBEnumTypeNode>()->is_complete);

  lldb::TypeSP type_sp = dwarf->MakeType(
      die.GetID(), ConstString(die.GetName()),
      /*byte_size=*/std::nullopt,
      /*context=*/nullptr,
      LLDB_INVALID_UID, Type::eEncodingIsUID, Declaration(), ct,
      needs_completion ? Type::ResolveState::Forward : Type::ResolveState::Full);

  // Register in the global DIE-to-type map so SymbolFileDWARF::CompleteType
  // can find this type when resolving forward declarations.
  dwarf->GetDIEToType()[die.GetDIE()] = type_sp.get();

  // Register incomplete Record/Enum types in the forward-declaration map so
  // that SymbolFileDWARF::CompleteType triggers lazy field population.
  if (needs_completion) {
    if (auto die_ref = die.GetDIERef())
      dwarf->GetForwardDeclCompilerTypeToDIE()
          .try_emplace((void *)node, *die_ref);
  }

  m_die_to_type_sp[offset] = type_sp;
  if (type_is_new_ptr)
    *type_is_new_ptr = true;
  return type_sp;
}

ConstString DWARFASTParserCpp::ConstructDemangledNameFromDWARF(
    const DWARFDIE &die) {
  if (!die)
    return ConstString();

  // Build a qualified name string like "::foo(int, const char*)" using the
  // DWARF declaration context plus the function parameters.
  DWARFDeclContext decl_ctx = die.GetDWARFDeclContext();
  const char *qname = decl_ctx.GetQualifiedName();
  if (!qname) {
    if (const char *name = die.GetName())
      return ConstString(name);
    return ConstString();
  }

  StreamString sstr;
  sstr << qname << "(";

  bool is_variadic = false;
  bool first_param = true;
  bool is_const = false;
  bool is_volatile = false;

  for (DWARFDIE child : die.children()) {
    if (child.Tag() == DW_TAG_formal_parameter) {
      // Check if artificial (the 'this' parameter).
      bool is_art = false;
      DWARFAttributes attrs = child.GetAttributes();
      for (size_t i = 0; i < attrs.Size(); ++i) {
        DWARFFormValue fv;
        if (attrs.AttributeAtIndex(i) == DW_AT_artificial &&
            attrs.ExtractFormValueAtIndex(i, fv))
          is_art = fv.Boolean();
      }
      if (is_art) {
        // Peel CV quals from the this pointer's pointee type.
        DWARFDIE this_type_die = child.GetReferencedDIE(DW_AT_type);
        if (this_type_die && this_type_die.Tag() == DW_TAG_pointer_type) {
          DWARFDIE pointee = this_type_die.GetReferencedDIE(DW_AT_type);
          while (pointee) {
            if (pointee.Tag() == DW_TAG_const_type) {
              is_const = true;
              pointee = pointee.GetReferencedDIE(DW_AT_type);
            } else if (pointee.Tag() == DW_TAG_volatile_type) {
              is_volatile = true;
              pointee = pointee.GetReferencedDIE(DW_AT_type);
            } else {
              break;
            }
          }
        }
        continue;
      }

      // Get the type name for this parameter.
      DWARFDIE pt_die = child.GetReferencedDIE(DW_AT_type);
      if (!pt_die)
        continue;
      LLDBQualType qt = GetOrParseQualType(pt_die);
      if (!qt.node)
        continue;
      CompilerType ct(m_ast.weak_from_this(),
                      (lldb::opaque_compiler_type_t)qt.node);
      if (qt.quals.is_const)
        ct = m_ast.AddConstModifier(ct.GetOpaqueQualType());
      if (qt.quals.is_volatile)
        ct = m_ast.AddVolatileModifier(ct.GetOpaqueQualType());
      if (!first_param)
        sstr << ", ";
      first_param = false;
      sstr << ct.GetTypeName();
    } else if (child.Tag() == DW_TAG_unspecified_parameters) {
      is_variadic = true;
    }
  }
  if (is_variadic) {
    if (!first_param)
      sstr << ", ";
    sstr << "...";
  }
  sstr << ")";
  if (is_const)
    sstr << " const";
  if (is_volatile)
    sstr << " volatile";

  return ConstString(sstr.GetString());
}

Function *
DWARFASTParserCpp::ParseFunctionFromDWARF(CompileUnit &comp_unit,
                                           const DWARFDIE &die,
                                           AddressRanges ranges) {
  if (!die || die.Tag() != DW_TAG_subprogram)
    return nullptr;
  if (ranges.empty())
    return nullptr;

  const char *name = nullptr;
  const char *mangled = nullptr;
  std::optional<int> decl_file, decl_line, decl_column;
  std::optional<int> call_file, call_line, call_column;
  DWARFExpressionList frame_base;
  llvm::DWARFAddressRangesVector unused;

  if (!die.GetDIENamesAndRanges(name, mangled, unused, decl_file, decl_line,
                                decl_column, call_file, call_line, call_column,
                                &frame_base))
    return nullptr;

  Mangled func_name;
  if (mangled)
    func_name.SetValue(ConstString(mangled));
  else if ((die.GetParent().Tag() == DW_TAG_compile_unit ||
            die.GetParent().Tag() == DW_TAG_partial_unit) &&
           Language::LanguageIsCPlusPlus(
               SymbolFileDWARF::GetLanguage(*die.GetCU())) &&
           name && strcmp(name, "main") != 0) {
    func_name.SetDemangledName(ConstructDemangledNameFromDWARF(die));
    func_name.SetMangledName(ConstString(name));
  } else {
    func_name.SetValue(ConstString(name));
  }

  Address func_addr = ranges[0].GetBaseAddress();
  auto func_sp = std::make_shared<Function>(
      &comp_unit, die.GetID(), die.GetID(), func_name,
      /*type=*/nullptr, std::move(func_addr), std::move(ranges));

  if (frame_base.IsValid())
    func_sp->GetFrameBaseExpression() = frame_base;
  comp_unit.AddFunction(func_sp);
  return func_sp.get();
}

bool DWARFASTParserCpp::CompleteTypeFromDWARF(
    const DWARFDIE &die, Type *type, const CompilerType &compiler_type) {
  if (!die)
    return false;
  // For records, parse members
  if (die.Tag() == DW_TAG_structure_type || die.Tag() == DW_TAG_class_type ||
      die.Tag() == DW_TAG_union_type) {
    // Prefer the node from compiler_type: that is the actual type the
    // caller wants to complete, which may differ from what this parser cached
    // for the same DIE offset when the type was parsed in a different module.
    LLDBTypeNode *node = nullptr;
    {
      auto ts = compiler_type.GetTypeSystem<TypeSystemCpp>();
      if (ts)
        node = TypeSystemCpp::GetNode(compiler_type);
    }
    if (!node) {
      auto dit = m_die_to_type.find(die.GetOffset());
      if (dit != m_die_to_type.end())
        node = dit->second;
    }
    if (!node || node->kind != TypeNodeKind::Record)
      return false;
    auto *rec = node->As<LLDBRecordTypeNode>();
    if (rec->is_complete)
      return true;
    // Record the complete definition DIE for later nested-type lookups.
    // Register this parser in the TypeSystem that OWNS the node (which may
    // be a different instance than m_ast when completing a cross-module type).
    m_type_to_die[node] = die;
    // Cache this die offset → node so that GetOrParseType for this exact
    // definition DIE returns the existing (possibly cross-module) node rather
    // than creating a duplicate.  Use assignment (not try_emplace) so that a
    // cross-module completion overwrites a stale entry from an earlier parse.
    m_die_to_type[die.GetOffset()] = node;
    {
      auto owning_ts = compiler_type.GetTypeSystem<TypeSystemCpp>();
      TypeSystemCpp *owner = owning_ts.get() ? owning_ts.get() : &m_ast;
      owner->SetCompleteParserForNode(node, this);
    }
    rec->is_complete = true;
    // If the DIE is a forward declaration (DW_AT_declaration), we couldn't
    // find a real definition — mark as forcefully completed so that
    // SBType::IsTypeComplete() returns false, matching TypeSystemClang behavior.
    // Also read the byte_size from the definition DIE (forward decls have no size).
    {
      DWARFAttributes die_attrs = die.GetAttributes();
      for (size_t i = 0; i < die_attrs.Size(); ++i) {
        dw_attr_t attr = die_attrs.AttributeAtIndex(i);
        DWARFFormValue fv;
        if (attr == DW_AT_declaration) {
          if (die_attrs.ExtractFormValueAtIndex(i, fv) && fv.Boolean())
            rec->is_forcefully_completed = true;
        } else if (attr == DW_AT_byte_size) {
          if (die_attrs.ExtractFormValueAtIndex(i, fv))
            rec->byte_size = fv.Unsigned();
        }
      }
    }

    for (DWARFDIE child : die.children()) {
      if (child.Tag() == DW_TAG_member) {
        const char *fname = child.GetName();
        DWARFDIE ft_die = child.GetReferencedDIE(DW_AT_type);
        LLDBQualType ft;
        if (ft_die)
          ft = GetOrParseQualType(ft_die);

        // Detect static members: they have DW_AT_external or no
        // DW_AT_data_member_location (instance fields always have a location).
        // Bitfields use DW_AT_data_bit_offset instead of DW_AT_data_member_location.
        bool has_data_location = false;
        bool is_external = false;
        bool is_declaration = false;
        DWARFAttributes attrs = child.GetAttributes();
        for (size_t i = 0; i < attrs.Size(); ++i) {
          switch (attrs.AttributeAtIndex(i)) {
          case DW_AT_data_member_location:
          case DW_AT_data_bit_offset:
          case DW_AT_bit_offset:
            has_data_location = true;
            break;
          case DW_AT_external: {
            DWARFFormValue fv;
            if (attrs.ExtractFormValueAtIndex(i, fv))
              is_external = fv.Boolean();
          } break;
          case DW_AT_declaration: {
            DWARFFormValue fv;
            if (attrs.ExtractFormValueAtIndex(i, fv))
              is_declaration = fv.Boolean();
          } break;
          default:
            break;
          }
        }

        if (is_external || is_declaration || !has_data_location) {
          // Static data member — read DW_AT_const_value if present.
          LLDBStaticMemberNode sm;
          sm.name = fname ? fname : "";
          sm.type = ft;
          for (size_t i = 0; i < attrs.Size(); ++i) {
            if (attrs.AttributeAtIndex(i) == DW_AT_const_value) {
              DWARFFormValue fv;
              if (attrs.ExtractFormValueAtIndex(i, fv)) {
                // DW_AT_const_value can be signed or unsigned; store both.
                bool is_signed = false;
                if (ft.node && ft.node->kind == TypeNodeKind::Builtin) {
                  auto *bt = ft.node->As<LLDBBuiltinTypeNode>();
                  if (bt)
                    is_signed = !bt->is_float && bt->is_signed;
                }
                llvm::APSInt val(llvm::APInt(64, fv.Unsigned()), !is_signed);
                sm.const_int_value = val;
              }
              break;
            }
          }
          rec->static_members.push_back(std::move(sm));
          continue;
        }

        LLDBFieldNode field;
        field.name = fname ? fname : "";
        field.type = ft;

        for (size_t i = 0; i < attrs.Size(); ++i) {
          DWARFFormValue fv;
          switch (attrs.AttributeAtIndex(i)) {
          case DW_AT_data_member_location:
            if (attrs.ExtractFormValueAtIndex(i, fv))
              field.bit_offset = fv.Unsigned() * 8;
            break;
          case DW_AT_data_bit_offset:
            if (attrs.ExtractFormValueAtIndex(i, fv))
              field.bit_offset = fv.Unsigned();
            break;
          case DW_AT_bit_offset:
            if (attrs.ExtractFormValueAtIndex(i, fv))
              field.bit_offset = fv.Unsigned();
            break;
          case DW_AT_bit_size:
            if (attrs.ExtractFormValueAtIndex(i, fv))
              field.bitfield_bit_size = (uint32_t)fv.Unsigned();
            break;
          case DW_AT_artificial:
            if (attrs.ExtractFormValueAtIndex(i, fv))
              field.is_artificial = fv.Boolean();
            break;
          default:
            break;
          }
        }
        rec->fields.push_back(std::move(field));
      } else if (child.Tag() == DW_TAG_inheritance) {
        DWARFDIE base_die = child.GetReferencedDIE(DW_AT_type);
        if (!base_die)
          continue;
        LLDBBaseClassNode base_class;
        base_class.type = LLDBQualType(GetOrParseType(base_die));
        DWARFAttributes attrs = child.GetAttributes();
        for (size_t i = 0; i < attrs.Size(); ++i) {
          DWARFFormValue fv;
          switch (attrs.AttributeAtIndex(i)) {
          case DW_AT_data_member_location:
            if (attrs.ExtractFormValueAtIndex(i, fv))
              base_class.bit_offset = fv.Unsigned() * 8;
            break;
          case DW_AT_virtuality:
            if (attrs.ExtractFormValueAtIndex(i, fv))
              base_class.is_virtual = fv.Boolean();
            break;
          case DW_AT_accessibility:
            if (attrs.ExtractFormValueAtIndex(i, fv))
              base_class.access =
                  GetAccessTypeFromDWARF((uint32_t)fv.Unsigned());
            break;
          default:
            break;
          }
        }
        rec->bases.push_back(std::move(base_class));
      } else if (child.Tag() == DW_TAG_subprogram) {
        const char *method_name = child.GetName();
        if (!method_name)
          continue;
        LLDBMethodNode method;
        method.name = method_name;

        // Get return type
        DWARFDIE ret_die = child.GetReferencedDIE(DW_AT_type);
        LLDBQualType ret_type;
        if (ret_die)
          ret_type = LLDBQualType(GetOrParseType(ret_die));
        else
          ret_type = LLDBQualType(m_ast.GetTypeRegistry().GetOrCreateBuiltin(
              lldb::eBasicTypeVoid, 0, false, false));

        // Collect parameters (skip the artificial 'this' pointer)
        std::vector<LLDBParamNode> params;
        bool is_variadic = false;
        bool has_this = false;
        for (DWARFDIE param : child.children()) {
          if (param.Tag() == DW_TAG_formal_parameter) {
            bool is_art = false;
            DWARFAttributes pattrs = param.GetAttributes();
            for (size_t pi = 0; pi < pattrs.Size(); ++pi) {
              DWARFFormValue fv;
              if (pattrs.AttributeAtIndex(pi) == DW_AT_artificial &&
                  pattrs.ExtractFormValueAtIndex(pi, fv))
                is_art = fv.Boolean();
            }
            const char *pname = param.GetName();
            if (is_art) {
              // Artificial parameters are implicit (e.g. 'this') - extract
              // CV quals from the this pointer's pointee type.
              has_this = true;
              DWARFDIE this_type_die = param.GetReferencedDIE(DW_AT_type);
              if (this_type_die &&
                  this_type_die.Tag() == DW_TAG_pointer_type) {
                // Peel through the full qualifier chain on the pointee.
                DWARFDIE pointee_die =
                    this_type_die.GetReferencedDIE(DW_AT_type);
                while (pointee_die) {
                  if (pointee_die.Tag() == DW_TAG_const_type) {
                    method.is_const = true;
                    pointee_die = pointee_die.GetReferencedDIE(DW_AT_type);
                  } else if (pointee_die.Tag() == DW_TAG_volatile_type) {
                    method.is_volatile = true;
                    pointee_die = pointee_die.GetReferencedDIE(DW_AT_type);
                  } else {
                    break;
                  }
                }
              }
              continue;
            }
            DWARFDIE pt_die = param.GetReferencedDIE(DW_AT_type);
            LLDBQualType pt;
            if (pt_die)
              pt = GetOrParseQualType(pt_die);
            params.push_back({pname ? pname : "", pt});
          } else if (param.Tag() == DW_TAG_unspecified_parameters) {
            is_variadic = true;
          }
        }
        method.is_static = !has_this;

        DWARFAttributes mattrs = child.GetAttributes();
        for (size_t mi = 0; mi < mattrs.Size(); ++mi) {
          DWARFFormValue fv;
          switch (mattrs.AttributeAtIndex(mi)) {
          case DW_AT_virtuality:
            if (mattrs.ExtractFormValueAtIndex(mi, fv))
              method.is_virtual = fv.Unsigned() != 0;
            break;
          case DW_AT_accessibility:
            if (mattrs.ExtractFormValueAtIndex(mi, fv))
              method.access = GetAccessTypeFromDWARF((uint32_t)fv.Unsigned());
            break;
          case DW_AT_artificial:
            if (mattrs.ExtractFormValueAtIndex(mi, fv))
              method.is_artificial = fv.Boolean();
            break;
          case DW_AT_reference:
            method.ref_qualifier = LLDBRefQualifier::LValue;
            break;
          case DW_AT_rvalue_reference:
            method.ref_qualifier = LLDBRefQualifier::RValue;
            break;
          default:
            break;
          }
        }
        auto *fn = m_ast.GetTypeRegistry().CreateFunctionType(
            ret_type, std::move(params), is_variadic);
        method.type = LLDBQualType(fn);
        if (!method.is_virtual)
          method.asm_label = MakeLLDBFuncAsmLabel(child);
        rec->methods.push_back(std::move(method));
      } else if (child.Tag() == DW_TAG_variable) {
        // In DWARF 4/5, static data members inside a class are DW_TAG_variable
        // with DW_AT_external=true and DW_AT_declaration=true.
        const char *vname = child.GetName();
        if (!vname)
          continue;
        bool is_external = false;
        bool is_declaration = false;
        lldb::AccessType access = lldb::eAccessPublic;
        std::optional<uint64_t> const_value_raw;
        DWARFAttributes vattrs = child.GetAttributes();
        for (size_t i = 0; i < vattrs.Size(); ++i) {
          DWARFFormValue fv;
          switch (vattrs.AttributeAtIndex(i)) {
          case DW_AT_external:
            if (vattrs.ExtractFormValueAtIndex(i, fv))
              is_external = fv.Boolean();
            break;
          case DW_AT_declaration:
            if (vattrs.ExtractFormValueAtIndex(i, fv))
              is_declaration = fv.Boolean();
            break;
          case DW_AT_accessibility:
            if (vattrs.ExtractFormValueAtIndex(i, fv))
              access = GetAccessTypeFromDWARF((uint32_t)fv.Unsigned());
            break;
          case DW_AT_const_value:
            if (vattrs.ExtractFormValueAtIndex(i, fv))
              const_value_raw = fv.Unsigned();
            break;
          default:
            break;
          }
        }
        if (!is_external && !is_declaration)
          continue;
        DWARFDIE vt_die = child.GetReferencedDIE(DW_AT_type);
        LLDBQualType vt;
        if (vt_die)
          vt = GetOrParseQualType(vt_die);
        LLDBStaticMemberNode sm;
        sm.name = vname;
        sm.type = vt;
        sm.access = access;
        if (const_value_raw) {
          bool is_signed = false;
          if (vt.node && vt.node->kind == TypeNodeKind::Builtin) {
            auto *bt = vt.node->As<LLDBBuiltinTypeNode>();
            if (bt)
              is_signed = !bt->is_float && bt->is_signed;
          }
          sm.const_int_value =
              llvm::APSInt(llvm::APInt(64, *const_value_raw), !is_signed);
        }
        rec->static_members.push_back(std::move(sm));
      }
    }
    // Synthesize unnamed bitfields for gaps that DWARF didn't emit.
    m_ast.SynthesizeUnnamedBitfields(rec);
    return true;
  }
  return false;
}

CompilerDecl
DWARFASTParserCpp::GetDeclForUIDFromDWARF(const DWARFDIE &die) {
  return CompilerDecl();
}

CompilerDeclContext
DWARFASTParserCpp::GetDeclContextForUIDFromDWARF(const DWARFDIE &die) {
  if (!die)
    return {};

  if (die.Tag() == DW_TAG_namespace) {
    LLDBNamespaceNode *ns = ResolveNamespaceDIE(die);
    return MakeNamespaceContext(ns);
  }

  // For types, return their enclosing namespace context
  LLDBNamespaceNode *ns = GetNamespaceForDIE(die);
  return MakeNamespaceContext(ns);
}

CompilerDeclContext DWARFASTParserCpp::GetDeclContextContainingUIDFromDWARF(
    const DWARFDIE &die) {
  if (!die)
    return {};

  // Walk up to find the parent context
  DWARFDIE parent = die.GetParent();
  while (parent) {
    if (parent.Tag() == DW_TAG_namespace) {
      LLDBNamespaceNode *ns = ResolveNamespaceDIE(parent);
      return MakeNamespaceContext(ns);
    }
    if (parent.Tag() == DW_TAG_compile_unit ||
        parent.Tag() == DW_TAG_partial_unit)
      break;
    parent = parent.GetParent();
  }
  // TU level -- return an invalid context so that
  // DIEInDeclContext(empty, die, ...) returns true
  return {};
}

void DWARFASTParserCpp::EnsureAllDIEsInDeclContextHaveBeenParsed(
    CompilerDeclContext decl_context) {
  // Not yet implemented; acceptable since we parse lazily
}

LLDBTypeNode *DWARFASTParserCpp::FindNestedTypedefByName(LLDBTypeNode *rec_node,
                                                          llvm::StringRef name) {
  // First check the already-parsed nested typedefs.
  if (rec_node && rec_node->kind == TypeNodeKind::Record) {
    for (auto *td : rec_node->As<LLDBRecordTypeNode>()->nested_typedefs) {
      if (td->name == name)
        return td;
    }
  }
  // Look up the complete definition DIE and search its typedef children.
  auto it = m_type_to_die.find(rec_node);
  if (it == m_type_to_die.end())
    return nullptr;
  DWARFDIE rec_die = it->second;
  if (!rec_die)
    return nullptr;
  for (DWARFDIE child : rec_die.children()) {
    if (child.Tag() != DW_TAG_typedef && child.Tag() != DW_TAG_template_alias)
      continue;
    const char *child_name = child.GetName();
    if (!child_name || name != child_name)
      continue;
    // Parse this typedef DIE; this also registers it in nested_typedefs.
    LLDBTypeNode *td_node = GetOrParseType(child);
    return td_node;
  }
  return nullptr;
}

std::string DWARFASTParserCpp::GetDIEClassTemplateParams(DWARFDIE die) {
  return {};
}
