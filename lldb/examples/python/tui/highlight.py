"""A small regular expression based syntax highlighter.

The highlighter splits source lines into (text, kind) tokens where kind is the
name of a color element (keyword, type, string, number, comment, preprocessor,
function) or 'text'. Multi-line comments and strings are supported by carrying
a state from one line to the next.
"""

import os
import re

_C_KEYWORDS = """
    auto break case const continue default do else enum extern for goto if
    inline register restrict return sizeof static struct switch typedef union
    volatile while _Alignas _Alignof _Atomic _Generic _Noreturn _Static_assert
    _Thread_local true false NULL nullptr
""".split()

_CPP_KEYWORDS = _C_KEYWORDS + """
    alignas alignof and and_eq asm bitand bitor catch class compl concept
    consteval constexpr constinit const_cast co_await co_return co_yield decltype
    delete dynamic_cast explicit export final friend mutable namespace new
    noexcept not not_eq operator or or_eq override private protected public
    reinterpret_cast requires static_assert static_cast template this
    thread_local throw try typeid typename using virtual xor xor_eq
""".split()

_OBJC_KEYWORDS = _CPP_KEYWORDS + """
    @interface @implementation @end @protocol @property @synthesize @dynamic
    @class @selector @encode @try @catch @finally @throw @autoreleasepool
    @synchronized @optional @required @public @private @protected @package
    self super nil Nil YES NO
""".split()

_C_TYPES = """
    void char short int long float double signed unsigned bool _Bool _Complex
    wchar_t char8_t char16_t char32_t size_t ssize_t ptrdiff_t intptr_t uintptr_t
    int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t FILE
    id instancetype SEL BOOL IMP Class
""".split()

_SWIFT_KEYWORDS = """
    associatedtype class deinit enum extension fileprivate func import init
    inout internal let open operator private precedencegroup protocol public
    rethrows static struct subscript typealias var break case catch continue
    default defer do else fallthrough for guard if in repeat return throw throws
    switch where while as await async false is nil self Self super true try
    actor any some mutating nonmutating lazy weak unowned override final
    required convenience dynamic optional indirect get set willSet didSet
""".split()

_SWIFT_TYPES = """
    Int Int8 Int16 Int32 Int64 UInt UInt8 UInt16 UInt32 UInt64 Float Double
    Bool String Character Array Dictionary Set Optional Void Any AnyObject Error
""".split()

_RUST_KEYWORDS = """
    as async await break const continue crate dyn else enum extern false fn for
    if impl in let loop match mod move mut pub ref return self Self static struct
    super trait true type union unsafe use where while
""".split()

_RUST_TYPES = """
    i8 i16 i32 i64 i128 isize u8 u16 u32 u64 u128 usize f32 f64 bool char str
    String Vec Option Result Box Rc Arc
""".split()

_PYTHON_KEYWORDS = """
    and as assert async await break class continue def del elif else except
    False finally for from global if import in is lambda None nonlocal not or
    pass raise return True try while with yield match case self
""".split()

_PYTHON_TYPES = "int float str bytes bool list dict set tuple object type".split()

_GO_KEYWORDS = """
    break case chan const continue default defer else fallthrough for func go
    goto if import interface map package range return select struct switch type
    var true false nil iota
""".split()

_GO_TYPES = """
    bool byte complex64 complex128 error float32 float64 int int8 int16 int32
    int64 rune string uint uint8 uint16 uint32 uint64 uintptr
""".split()

_NUMBER = (
    r"(?:0[xX][0-9a-fA-F']+(?:\.[0-9a-fA-F']*)?(?:[pP][+-]?\d+)?"
    r"|0[bB][01'_]+|0[oO][0-7_]+"
    r"|(?:\d[\d'_]*\.?[\d'_]*|\.\d[\d'_]*)(?:[eE][+-]?\d+)?)"
    r"[uUlLfFzZ]*(?:i\d+|u\d+|f\d+|usize|isize)?"
)


class Language(object):
    def __init__(
        self,
        name,
        keywords,
        types,
        line_comment="//",
        block_comment=("/*", "*/"),
        strings=('"', "'"),
        multiline_strings=(),
        preprocessor=False,
        ident=r"[A-Za-z_][A-Za-z0-9_]*",
    ):
        self.name = name
        self.keywords = frozenset(keywords)
        self.types = frozenset(types)
        self.line_comment = line_comment
        self.block_comment = block_comment
        self.multiline_strings = multiline_strings
        self.preprocessor = preprocessor
        parts = []
        if line_comment:
            parts.append(r"(?P<comment>%s.*)" % re.escape(line_comment))
        if block_comment:
            parts.append(r"(?P<block>%s)" % re.escape(block_comment[0]))
        for delimiter in multiline_strings:
            parts.append(
                r"(?P<mstring_%d>%s)"
                % (len(parts), re.escape(delimiter))
            )
        string_parts = []
        for quote in strings:
            q = re.escape(quote)
            string_parts.append(r"%s(?:\\.|[^%s\\])*(?:%s|$)" % (q, q, q))
        parts.append(r"(?P<string>(?:[LuUR@b]|u8|r#*)?(?:%s))" % "|".join(string_parts))
        parts.append(r"(?P<number>\b%s)" % _NUMBER)
        parts.append(r"(?P<ident>@?%s)(?P<call>\s*\()?" % ident)
        self.pattern = re.compile("|".join(parts))
        self.mstring_groups = dict(
            (name, name) for name in self.pattern.groupindex if name.startswith("mstring_")
        )


LANGUAGES = {
    "c": Language("c", _C_KEYWORDS, _C_TYPES, preprocessor=True),
    "c++": Language("c++", _CPP_KEYWORDS, _C_TYPES, preprocessor=True),
    "objc": Language("objc", _OBJC_KEYWORDS, _C_TYPES, preprocessor=True),
    "swift": Language(
        "swift", _SWIFT_KEYWORDS, _SWIFT_TYPES, strings=('"',),
        multiline_strings=('"""',)
    ),
    "rust": Language("rust", _RUST_KEYWORDS, _RUST_TYPES, strings=('"',)),
    "go": Language("go", _GO_KEYWORDS, _GO_TYPES, strings=('"', "'", "`")),
    "python": Language(
        "python", _PYTHON_KEYWORDS, _PYTHON_TYPES, line_comment="#",
        block_comment=None, multiline_strings=('"""', "'''")
    ),
}

_EXTENSIONS = {
    ".c": "c",
    ".h": "c++",
    ".cc": "c++",
    ".cpp": "c++",
    ".cxx": "c++",
    ".c++": "c++",
    ".hh": "c++",
    ".hpp": "c++",
    ".hxx": "c++",
    ".h++": "c++",
    ".inc": "c++",
    ".def": "c++",
    ".ipp": "c++",
    ".tpp": "c++",
    ".m": "objc",
    ".mm": "objc",
    ".swift": "swift",
    ".rs": "rust",
    ".go": "go",
    ".py": "python",
}


def language_for_file(path):
    if not path:
        return None
    _, extension = os.path.splitext(path.lower())
    if extension in _EXTENSIONS:
        return LANGUAGES[_EXTENSIONS[extension]]
    # Headers of the C++ standard library usually have no extension.
    if extension == "" and os.sep + "include" + os.sep in path:
        return LANGUAGES["c++"]
    return None


def language_by_name(name):
    if not name:
        return None
    name = name.lower()
    aliases = {"c++": "c++", "cplusplus": "c++", "objective-c": "objc",
               "objective-c++": "objc", "objc++": "objc"}
    for key in (aliases.get(name), name):
        if key in LANGUAGES:
            return LANGUAGES[key]
    for key in LANGUAGES:
        if name.startswith(key):
            return LANGUAGES[key]
    return None


def highlight_line(language, line, state=None):
    """Tokenize a line.

    Returns (tokens, state) where tokens is a list of (text, kind) and state
    has to be passed to the call for the next line. state is None or the
    delimiter that ends the comment/string the line ends in.
    """
    tokens = []
    position = 0
    length = len(line)

    def add(text, kind):
        if text:
            if tokens and tokens[-1][1] == kind:
                tokens[-1] = (tokens[-1][0] + text, kind)
            else:
                tokens.append((text, kind))

    if state is not None:
        kind = "comment" if language.block_comment and state == language.block_comment[1] else "string"
        end = line.find(state)
        if end < 0:
            add(line, kind)
            return tokens, state
        add(line[: end + len(state)], kind)
        position = end + len(state)
        state = None

    if language.preprocessor and position == 0:
        match = re.match(r"\s*#\s*\w+", line)
        if match:
            add(match.group(0), "preprocessor")
            position = match.end()
            include = re.match(r"\s*<[^>]*>", line[position:])
            if include:
                add(include.group(0), "string")
                position += include.end()

    while position < length:
        match = language.pattern.search(line, position)
        if not match:
            add(line[position:], "text")
            break
        add(line[position : match.start()], "text")
        group = match.lastgroup
        if group == "call":
            group = "ident"
        if group == "comment":
            add(match.group(0), "comment")
        elif group == "block":
            end = line.find(language.block_comment[1], match.end())
            if end < 0:
                add(line[match.start() :], "comment")
                return tokens, language.block_comment[1]
            end += len(language.block_comment[1])
            add(line[match.start() : end], "comment")
            position = end
            continue
        elif group in language.mstring_groups:
            delimiter = match.group(0)
            end = line.find(delimiter, match.end())
            if end < 0:
                add(line[match.start() :], "string")
                return tokens, delimiter
            end += len(delimiter)
            add(line[match.start() : end], "string")
            position = end
            continue
        elif group == "string":
            add(match.group(0), "string")
        elif group == "number":
            add(match.group(0), "number")
        else:
            word = match.group("ident")
            if word in language.keywords:
                kind = "keyword"
            elif word in language.types:
                kind = "type"
            elif match.group("call"):
                kind = "function"
            else:
                kind = "text"
            add(word, kind)
            add(match.group("call") or "", "text")
        position = match.end()
    return tokens, state


def highlight_lines(language, lines):
    """Tokenize all lines of a file. Returns a list of token lists."""
    result = []
    state = None
    for line in lines:
        tokens, state = highlight_line(language, line, state)
        result.append(tokens)
    return result


_ASM_TOKEN = re.compile(
    r"(?P<comment>[;#].*)"
    r"|(?P<number>\b(?:0x[0-9a-fA-F]+|\d+)\b)"
    r"|(?P<register>%?\b[a-z][a-z0-9]*\b)"
)


def highlight_operands(text):
    """Tokenize the operands of a disassembled instruction."""
    tokens = []
    position = 0
    for match in _ASM_TOKEN.finditer(text):
        if match.start() > position:
            tokens.append((text[position : match.start()], "text"))
        kind = match.lastgroup
        tokens.append((match.group(0), {"register": "type"}.get(kind, kind)))
        position = match.end()
    if position < len(text):
        tokens.append((text[position:], "text"))
    return tokens
