"""
Test lldb behaves sanely when formatting a corrupted `std::map`.

An uninitialized `std::map` on the stack (or one described by bad debug info)
can claim an arbitrary number of nodes in `__size_` while its `__left_` chain
never reaches a null pointer. The formatter walks that chain when it iterates
the tree, and every step of the walk creates a ValueObject parented on the one
from the previous step, so the walk has to be bounded by the *height* a
red-black tree of that size could have -- bounding it by the node count instead
makes printing such a map take quadratic time in tens of millions of steps,
i.e. hang the debugger.
"""

import time

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class LibcxxCorruptMapDataFormatterTestCase(TestBase):
    # A node count no real map could have at this address, but that an
    # uninitialized one easily has.
    FAKE_SIZE = 100000

    # How long printing the corrupted map may take. With the height bound the
    # walk gives up after a few dozen steps and the command is instant; without
    # it the walk takes FAKE_SIZE steps whose cost grows with the number of
    # steps already taken, which is minutes at best.
    TIME_LIMIT = 30

    def tree_of(self, name):
        """The raw (non-synthetic) `__tree_` member of the map named `name`."""
        var = self.frame().FindVariable(name)
        self.assertTrue(var.IsValid(), "found %s" % name)
        tree = var.GetNonSyntheticValue().GetChildMemberWithName("__tree_")
        self.assertTrue(tree.IsValid(), "%s has a __tree_ member" % name)
        return tree

    def inflate_size(self, tree):
        """Make the tree claim FAKE_SIZE nodes."""
        size = tree.GetChildMemberWithName("__size_")
        if not size.IsValid():
            self.skipTest("this libc++ does not store the size in __size_")
        self.assertTrue(size.SetValueFromCString(str(self.FAKE_SIZE)))

    @add_test_categories(["libc++"])
    def test_inflated_size(self):
        """A bogus node count must not stop the real elements from printing.

        This is the other side of the height bound: it has to stay wide enough
        for every tree walk a well-formed map needs.
        """
        self.build()
        lldbutil.run_to_source_breakpoint(
            self, "Set break point at this line.", lldb.SBFileSpec("main.cpp")
        )

        self.inflate_size(self.tree_of("inflated_size"))

        # Only ask for the children that really exist; what the formatter makes
        # of the ones past the end of the tree is not what this test is about.
        self.runCmd("settings set target.max-children-count 3")
        self.expect(
            "frame variable inflated_size",
            substrs=[
                "size=%d" % self.FAKE_SIZE,
                "[0] = (first = 1, second = 11)",
                "[1] = (first = 2, second = 22)",
                "[2] = (first = 3, second = 33)",
            ],
        )

    @add_test_categories(["libc++"])
    def test_cyclic_tree(self):
        """Printing a map whose __left_ chain is a cycle has to terminate."""
        self.build()
        (target, process, thread, bkpt) = lldbutil.run_to_source_breakpoint(
            self, "Set break point at this line.", lldb.SBFileSpec("main.cpp")
        )

        tree = self.tree_of("cyclic_tree")
        self.inflate_size(tree)

        # Point both __left_ (at offset 0 of a __tree_node_base) and __right_
        # (at offset one pointer) of the first node back at the node itself, so
        # that walking either one never reaches a null pointer and never fails
        # to read. See the layout comment at the top of LibCxxMap.cpp.
        begin = tree.GetChildMemberWithName("__begin_node_")
        self.assertTrue(begin.IsValid(), "cyclic_tree has a __begin_node_")
        node = begin.GetValueAsUnsigned()
        self.assertNotEqual(node, 0)

        ptr_size = process.GetAddressByteSize()
        order = "little" if target.GetByteOrder() == lldb.eByteOrderLittle else "big"
        self_ptr = node.to_bytes(ptr_size, byteorder=order)
        error = lldb.SBError()
        process.WriteMemory(node, self_ptr + self_ptr, error)
        self.assertSuccess(error, "corrupted the first node")

        # The formatter has to give up on the tree instead of walking the cycle
        # FAKE_SIZE times; all this asserts is that the command comes back, and
        # that it comes back quickly. A regression shows up either as the time
        # limit below being blown or, on a slow enough walk, as this test timing
        # out.
        start = time.monotonic()
        self.expect(
            "frame variable cyclic_tree", substrs=["size=%d" % self.FAKE_SIZE]
        )
        elapsed = time.monotonic() - start
        self.assertLess(
            elapsed,
            self.TIME_LIMIT,
            "printing a map with a cyclic __left_ chain took %.1fs" % elapsed,
        )
