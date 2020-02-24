from lldbsuite.test import lldbinline
from lldbsuite.test import decorators

lldbinline.MakeInlineTest(__file__, globals(), [decorators.skipIf(debug_info=decorators.no_match(["dwarf"]))])
