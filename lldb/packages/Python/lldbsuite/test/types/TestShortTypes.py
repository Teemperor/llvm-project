"""
Test that variables of type short are displayed correctly.
"""

import AbstractBase

from lldbsuite.test.decorators import *

class IntegerTypesTestCase(AbstractBase.GenericTester):

    mydir = AbstractBase.GenericTester.compute_mydir(__file__)

    def setUp(self):
        AbstractBase.GenericTester.setUp(self)

    def test_short_type(self):
        """Test that short-type variables are displayed correctly."""
        self.build_and_run('short.cpp', set(['short']))

    @skipUnlessDarwin
    def test_short_type_from_block(self):
        """Test that short-type variables are displayed correctly from a block."""
        self.build_and_run('short.cpp', set(['short']), bc=True)
