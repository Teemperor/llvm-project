"""
Test that variables of type long are displayed correctly.
"""

import AbstractBase

from lldbsuite.test.decorators import *

class IntegerTypesTestCase(AbstractBase.GenericTester):

    mydir = AbstractBase.GenericTester.compute_mydir(__file__)

    def setUp(self):
        AbstractBase.GenericTester.setUp(self)

    def test_long_type(self):
        """Test that long-type variables are displayed correctly."""
        self.build_and_run('long.cpp', set(['long']))

    @skipUnlessDarwin
    def test_long_type_from_block(self):
        """Test that long-type variables are displayed correctly from a block."""
        self.build_and_run('long.cpp', set(['long']), bc=True)
