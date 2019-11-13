"""
Test SBModule's IsDebugInfoCompatible.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class ModuleDebugInfoCheckTestCase(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    def assert_invalid_module_err(self, error):
        self.assertEquals("invalid module", error.GetCString())
        self.assertFalse(error.Success())

    # Py3 asserts due to a bug in SWIG.  A fix for this was upstreamed into
    # SWIG 3.0.8.
    @skipIf(py_version=['>=', (3, 0)], swig_version=['<', (3, 0, 8)])
    @add_test_categories(['pyapi'])
    @no_debug_info_test
    def test_default_module(self):
        exe_module = lldb.SBModule()
        self.assert_invalid_module_err(exe_module.IsDebugInfoCompatible(lldb.eLanguageTypeUnknown))
        self.assert_invalid_module_err(error = exe_module.IsDebugInfoCompatible(lldb.eLanguageTypeC))

    def assert_compatible(self, exe_module, lang):
        error = exe_module.IsDebugInfoCompatible(lang)
        self.assertTrue(error.Success())

    # Py3 asserts due to a bug in SWIG.  A fix for this was upstreamed into
    # SWIG 3.0.8.
    @skipIf(py_version=['>=', (3, 0)], swig_version=['<', (3, 0, 8)])
    @add_test_categories(['pyapi'])
    @no_debug_info_test
    def test_c_languages(self):
        self.build()
        exe = self.getBuildArtifact("a.out")

        target = self.dbg.CreateTarget(exe)
        self.assertTrue(target, VALID_TARGET)
        self.assertTrue(target.GetNumModules() > 0)

        exe_module = target.GetModuleAtIndex(0)

        # Check that we get an error if LLDB doesn't implement the language
        error = exe_module.IsDebugInfoCompatible(lldb.eLanguageTypeModula2)
        self.assertEquals("TypeSystem for language modula2 doesn't exist", error.GetCString())
        self.assertFalse(error.Success())

        # Check that C languages are compatible with the module.
        self.assert_compatible(exe_module, lldb.eLanguageTypeC)
        self.assert_compatible(exe_module, lldb.eLanguageTypeC89)
        self.assert_compatible(exe_module, lldb.eLanguageTypeC99)
        self.assert_compatible(exe_module, lldb.eLanguageTypeC11)
        self.assert_compatible(exe_module, lldb.eLanguageTypeC_plus_plus)
        self.assert_compatible(exe_module, lldb.eLanguageTypeC_plus_plus_03)
        self.assert_compatible(exe_module, lldb.eLanguageTypeC_plus_plus_11)
        self.assert_compatible(exe_module, lldb.eLanguageTypeC_plus_plus_14)

        # 'Unknown' language gets mapped to a generic C language right now.
        self.assert_compatible(exe_module, lldb.eLanguageTypeUnknown)
