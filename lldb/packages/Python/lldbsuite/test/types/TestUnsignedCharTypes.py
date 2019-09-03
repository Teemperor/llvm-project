"""
Test that variables of type unsigned char are displayed correctly.
"""

import AbstractBase

from lldbsuite.test.decorators import *

class IntegerTypesTestCase(AbstractBase.GenericTester):

    mydir = AbstractBase.GenericTester.compute_mydir(__file__)

    def setUp(self):
        AbstractBase.GenericTester.setUp(self)

    def test_unsigned_char_type(self):
        """Test that 'unsigned_char'-type variables are displayed correctly."""
        self.build_and_run('unsigned_char.cpp', set(
            ['unsigned', 'char']), qd=True)

    @skipUnlessDarwin
    def test_unsigned_char_type_from_block(self):
        """Test that 'unsigned char'-type variables are displayed correctly from a block."""
        self.build_and_run('unsigned_char.cpp', set(
            ['unsigned', 'char']), bc=True, qd=True)
