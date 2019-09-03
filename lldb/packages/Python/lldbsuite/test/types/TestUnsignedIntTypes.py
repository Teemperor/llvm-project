"""
Test that variables of type unsigned int are displayed correctly.
"""

import AbstractBase

from lldbsuite.test.decorators import *

class IntegerTypesTestCase(AbstractBase.GenericTester):

    mydir = AbstractBase.GenericTester.compute_mydir(__file__)

    def setUp(self):
        AbstractBase.GenericTester.setUp(self)

    def test_unsigned_int_type(self):
        """Test that 'unsigned_int'-type variables are displayed correctly."""
        self.build_and_run('unsigned_int.cpp', set(['unsigned', 'int']))

    @skipUnlessDarwin
    def test_unsigned_int_type_from_block(self):
        """Test that 'unsigned int'-type variables are displayed correctly from a block."""
        self.build_and_run('unsigned_int.cpp', set(
            ['unsigned', 'int']), bc=True)
