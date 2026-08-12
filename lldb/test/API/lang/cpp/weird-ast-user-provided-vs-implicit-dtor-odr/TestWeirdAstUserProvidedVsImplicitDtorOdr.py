import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstUserProvidedVsImplicitDtorOdrTestCase(TestBase):
    def test_implicit_dtor_alone(self):
        """
        Just completing the dylib's 'Logger' (which has no user-declared
        destructor, so it gets an implicit trivial one) on its own, at a
        breakpoint inside the dylib, works fine as long as the exe's
        conflicting, user-provided-destructor 'Logger' hasn't already been
        imported into the shared per-target scratch AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("plugin_logger.id", result_type="int", result_value="42")

        # Force LLDB to reason about a *local*, stack-allocated 'Logger' of
        # the dylib's incomplete-destructor flavour: this requires deciding
        # Logger::hasTrivialDestructor() and instantiating/synthesizing an
        # implicit destructor for it.
        self.expect("expression Logger l; (void)l;")

    @expectedFailureAll(
        bugnumber="ASTImporter/TypeSystemClang can't cleanly reconcile a "
        "user-provided, non-trivial destructor on one module's definition "
        "of a class with an implicit, trivial destructor on another "
        "module's ODR-violating 'same' definition: once the exe's "
        "user-provided destructor has been merged into the shared scratch "
        "AST context, forcing completion of the dylib's implicit "
        "destructor on the 'same' canonical RecordDecl fails with "
        "'destructor cannot be redeclared' instead of silently picking one "
        "definition (as happens for many other kinds of ODR violations)"
    )
    def test_implicit_vs_user_provided_dtor_conflict(self):
        """
        Tests LLDB's behaviour when a class ('Logger') has a user-provided,
        non-trivial, out-of-line destructor in the main executable, but an
        ODR-violating 'same' definition in a dylib has no user-declared
        destructor at all (and thus gets an implicit, trivial one
        instead). Both definitions otherwise have identical field layout
        ('int id;').

        CXXRecordDecl::hasTrivialDestructor() (and related bits, such as
        whether the destructor is user-declared) are cached once, in the
        class's DefinitionData, at completion time
        (CXXRecordDecl::completeDefinition()). LLDB's
        DWARFASTParserClang/ASTImporter machinery instead fabricates class
        definitions post-hoc from independent DWARF definitions in each
        module, and can end up trying to complete/attach an implicit
        destructor to the very same (merged) canonical RecordDecl that
        already has a user-provided, non-trivial, out-of-line destructor
        attached from the other module's conflicting definition.

        This exercises that conflict directly: first force the exe's
        user-provided-destructor 'Logger' into the shared scratch AST
        context (via a local variable at a breakpoint inside the dylib),
        then force completion of the dylib's implicit-trivial-destructor
        'Logger' on top of it by explicitly invoking its destructor.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Pull the exe's conflicting 'Logger' (user-provided, non-trivial,
        # out-of-line destructor) into the shared scratch AST context
        # first, via a local variable.
        self.expect("expression Logger l; (void)l;")

        # Now force LLDB to complete/attach an implicit destructor for the
        # dylib's conflicting 'Logger' (no user-declared destructor, so a
        # trivial one is implied) onto what Clang considers the 'same'
        # canonical RecordDecl -- by explicitly invoking it. This is the
        # exact moment CXXRecordDecl::completeDefinition() gets invoked a
        # second time with conflicting DefinitionData for what should be a
        # single, consistent class definition.
        self.expect_expr("plugin_logger.id", result_type="int", result_value="42")
        self.expect("expression plugin_logger.~Logger()")

        # Dump the per-module ASTs and the shared scratch AST context
        # while both conflicting definitions are alive, to poke at any
        # state that got merged/reconciled across the two. Whatever the
        # outcome, this should not crash LLDB.
        self.expect("target modules dump ast --filter Logger")
        self.expect("target dump typesystem")

        # Also make sure the exe's own (user-provided, non-trivial)
        # destructor can still be invoked afterwards without crashing.
        self.expect("expression global_logger.~Logger()")
