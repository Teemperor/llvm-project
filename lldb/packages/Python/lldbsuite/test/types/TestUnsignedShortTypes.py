"""
Test that variables of type unsigned short are displayed correctly.
"""

import AbstractBase

from lldbsuite.test.decorators import *

class IntegerTypesTestCase(AbstractBase.GenericTester):

    mydir = AbstractBase.GenericTester.compute_mydir(__file__)

    def setUp(self):
        AbstractBase.GenericTester.setUp(self)

    def test_unsigned_short_type(self):
        """Test that 'unsigned_short'-type variables are displayed correctly."""
        self.build_and_run('unsigned_short.cpp', set(['unsigned', 'short']))

    @skipUnlessDarwin
    def test_unsigned_short_type_from_block(self):
        """Test that 'unsigned short'-type variables are displayed correctly from a block."""
        self.build_and_run('unsigned_short.cpp', set(
            ['unsigned', 'short']), bc=True)
