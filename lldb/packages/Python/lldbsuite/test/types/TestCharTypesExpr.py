"""
Test that variable expressions of type char are evaluated correctly.
"""

import AbstractBase

from lldbsuite.test.decorators import *

class IntegerTypesExprTestCase(AbstractBase.GenericTester):

    mydir = AbstractBase.GenericTester.compute_mydir(__file__)

    def test_char_type(self):
        """Test that char-type variable expressions are evaluated correctly."""
        self.build_and_run_expr('char.cpp', set(['char']), qd=True)

    @skipUnlessDarwin
    def test_char_type_from_block(self):
        """Test that char-type variables are displayed correctly from a block."""
        self.build_and_run_expr('char.cpp', set(['char']), bc=True, qd=True)
