class Option<string fullname, string shortname> {
  string FullName = fullname;
  string ShortName = shortname;
}

class Desc<string description> {
  string Description = description;
}

class Required {
  bit Required = 1;
}

class OptionalArgument<string type> {
  string ArgType = type;
  bit OptionalArg = 1;
}

class Argument<string type> {
  string ArgType = type;
}

class Completion<list<string> completions> {
  list<string> Completions = completions;
}
