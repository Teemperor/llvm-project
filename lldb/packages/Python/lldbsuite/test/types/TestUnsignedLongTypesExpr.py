"""
Test that variable expressions of type unsigned long are evaluated correctly.
"""

import AbstractBase

from lldbsuite.test.decorators import *

class IntegerTypesExprTestCase(AbstractBase.GenericTester):

    mydir = AbstractBase.GenericTester.compute_mydir(__file__)

    def test_unsigned_long_type(self):
        """Test that 'unsigned long'-type variable expressions are evaluated correctly."""
        self.build_and_run_expr('unsigned_long.cpp', set(['unsigned', 'long']))

    @skipUnlessDarwin
    def test_unsigned_long_type_from_block(self):
        """Test that 'unsigned_long'-type variables are displayed correctly from a block."""
        self.build_and_run_expr('unsigned_long.cpp', set(
            ['unsigned', 'long']), bc=True)
