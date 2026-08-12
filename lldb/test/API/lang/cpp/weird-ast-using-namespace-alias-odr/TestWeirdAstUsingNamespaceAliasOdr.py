"""
Test LLDB's handling of an ODR conflict on a namespace *alias* rather than
on the tag name itself: both main.cpp and plugin.cpp define a namespace
alias called 'config', but each alias points at a different underlying
namespace ('impl_v1' vs 'impl_v2') containing an incompatibly-sized
'Config' struct. The qualified expression 'config::Config' is lexically
identical in both translation units but refers to two genuinely different
types depending on which alias target is actually in scope for the
currently selected frame.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstUsingNamespaceAliasOdrTestCase(TestBase):
    def test_alias_in_defining_frame(self):
        """
        Evaluating 'config::Config' while stopped in the frame that actually
        defines that alias (main.cpp's 'a_func', where 'config' is an alias
        for 'impl_v1') resolves to the expected, correctly-sized type.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(self, "a_func", lldb.SBFileSpec("main.cpp"))

        self.expect_expr("config::Config{}.flags", result_type="int", result_value="0")
        self.expect_expr("sizeof(config::Config)", result_type="unsigned long", result_value="4")

    @expectedFailureAll(
        bugnumber="LLDB's expression evaluator resolves a qualified name "
        "like 'config::Config' via a namespace alias that is looked up by "
        "name only, without regard to which frame/module actually defines "
        "that alias. Once the main executable's 'namespace config = "
        "impl_v1;' has been used in an expression, later switching to a "
        "frame in a dylib that defines its own unrelated 'namespace config "
        "= impl_v2;' still resolves 'config::Config' to the main "
        "executable's impl_v1::Config, so lookups of members that only "
        "exist on the dylib's impl_v2::Config (and sizeof queries) silently "
        "use the wrong, stale namespace-alias target instead of the one "
        "belonging to the currently selected frame."
    )
    def test_alias_in_other_module_frame(self):
        """
        Tests LLDB's behavior when we first resolve 'config::Config' while
        stopped in main.cpp's frame (whose 'config' alias targets
        'impl_v1'), then switch to plugin.cpp's frame (whose distinct
        'config' alias targets 'impl_v2') and evaluate 'config::Config'
        again. Ideally each frame's expression should resolve 'config' to
        its own frame-appropriate target namespace.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(self, "a_func", lldb.SBFileSpec("main.cpp"))

        # First resolve 'config::Config' while in main.cpp's frame, where
        # 'config' is an alias for 'impl_v1'.
        self.expect_expr("config::Config{}.flags", result_type="int", result_value="0")

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Now that we're stopped in plugin.cpp's frame, 'config' should be
        # plugin.cpp's alias for 'impl_v2', whose 'Config' has an
        # 'extra_flags' member that impl_v1::Config lacks.
        self.expect_expr(
            "config::Config{}.extra_flags", result_type="int", result_value="0"
        )

        # 'sizeof' should likewise reflect the larger, two-int
        # 'impl_v2::Config' while stopped in plugin.cpp's frame.
        self.expect_expr(
            "sizeof(config::Config)", result_type="unsigned long", result_value="8"
        )
