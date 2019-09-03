"""
Test that variable expressions of type unsigned short are evaluated correctly.
"""

import AbstractBase

from lldbsuite.test.decorators import *

class IntegerTypesExprTestCase(AbstractBase.GenericTester):

    mydir = AbstractBase.GenericTester.compute_mydir(__file__)

    def test_unsigned_short_type(self):
        """Test that 'unsigned_short'-type variable expressions are evaluated correctly."""
        self.build_and_run_expr('unsigned_short.cpp',
                                set(['unsigned', 'short']))

    @skipUnlessDarwin
    def test_unsigned_short_type_from_block(self):
        """Test that 'unsigned short'-type variables are displayed correctly from a block."""
        self.build_and_run_expr('unsigned_short.cpp', set(
            ['unsigned', 'short']), bc=True)
