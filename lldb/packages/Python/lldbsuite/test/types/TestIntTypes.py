"""
Test that variables of type int are displayed correctly.
"""

import AbstractBase

from lldbsuite.test.decorators import *

class IntegerTypesTestCase(AbstractBase.GenericTester):

    mydir = AbstractBase.GenericTester.compute_mydir(__file__)

    def setUp(self):
        AbstractBase.GenericTester.setUp(self)

    def test_int_type(self):
        """Test that int-type variables are displayed correctly."""
        self.build_and_run('int.cpp', set(['int']))

    @skipUnlessDarwin
    def test_int_type_from_block(self):
        """Test that int-type variables are displayed correctly from a block."""
        self.build_and_run('int.cpp', set(['int']))
