"""
Test that variables of type unsigned long are displayed correctly.
"""

import AbstractBase

from lldbsuite.test.decorators import *

class IntegerTypesTestCase(AbstractBase.GenericTester):

    mydir = AbstractBase.GenericTester.compute_mydir(__file__)

    def setUp(self):
        AbstractBase.GenericTester.setUp(self)

    def test_unsigned_long_type(self):
        """Test that 'unsigned long'-type variables are displayed correctly."""
        self.build_and_run('unsigned_long.cpp', set(['unsigned', 'long']))

    @skipUnlessDarwin
    def test_unsigned_long_type_from_block(self):
        """Test that 'unsigned_long'-type variables are displayed correctly from a block."""
        self.build_and_run('unsigned_long.cpp', set(
            ['unsigned', 'long']), bc=True)
