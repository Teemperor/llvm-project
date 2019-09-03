"""
Test that variable expressions of type int are evaluated correctly.
"""

import AbstractBase

from lldbsuite.test.decorators import *

class IntegerTypesExprTestCase(AbstractBase.GenericTester):

    mydir = AbstractBase.GenericTester.compute_mydir(__file__)

    def test_int_type(self):
        """Test that int-type variable expressions are evaluated correctly."""
        self.build_and_run_expr('int.cpp', set(['int']))

    @skipUnlessDarwin
    def test_int_type_from_block(self):
        """Test that int-type variables are displayed correctly from a block."""
        self.build_and_run_expr('int.cpp', set(['int']))
