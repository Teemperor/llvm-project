"""
Test that variables of type char are displayed correctly.
"""

import AbstractBase

from lldbsuite.test.decorators import *

class IntegerTypesTestCase(AbstractBase.GenericTester):

    mydir = AbstractBase.GenericTester.compute_mydir(__file__)

    def test_char_type(self):
        """Test that char-type variables are displayed correctly."""
        self.build_and_run('char.cpp', set(['char']), qd=True)

    @skipUnlessDarwin
    def test_char_type_from_block(self):
        """Test that char-type variables are displayed correctly from a block."""
        self.build_and_run('char.cpp', set(['char']), bc=True, qd=True)
