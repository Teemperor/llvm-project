"""
Test that variable expressions of type long are evaluated correctly.
"""

import AbstractBase

from lldbsuite.test.decorators import *

class IntegerTypesExprTestCase(AbstractBase.GenericTester):

    mydir = AbstractBase.GenericTester.compute_mydir(__file__)

    def test_long_type(self):
        """Test that long-type variable expressions are evaluated correctly."""
        self.build_and_run_expr('long.cpp', set(['long']))

    @skipUnlessDarwin
    def test_long_type_from_block(self):
        """Test that long-type variables are displayed correctly from a block."""
        self.build_and_run_expr('long.cpp', set(['long']), bc=True)
