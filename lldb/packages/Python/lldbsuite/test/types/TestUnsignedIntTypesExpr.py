"""
Test that variable expressions of type unsigned int are evaluated correctly.
"""

import AbstractBase

from lldbsuite.test.decorators import *

class IntegerTypesExprTestCase(AbstractBase.GenericTester):

    mydir = AbstractBase.GenericTester.compute_mydir(__file__)

    def test_unsigned_int_type(self):
        """Test that 'unsigned_int'-type variable expressions are evaluated correctly."""
        self.build_and_run_expr('unsigned_int.cpp', set(['unsigned', 'int']))

    @skipUnlessDarwin
    def test_unsigned_int_type_from_block(self):
        """Test that 'unsigned int'-type variables are displayed correctly from a block."""
        self.build_and_run_expr(
            'unsigned_int.cpp', set(['unsigned', 'int']), bc=True)
