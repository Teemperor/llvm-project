include "OptionsBase.td"

let Command = "help" in {
  def hide_aliases : Option<"hide-aliases", "a">,
    Desc<"Hide aliases in the command list.">;
  def hide_user : Option<"hide-user-commands", "u">;
  def show_hidden : Option<"show-hidden-commands", "h">;
}

let Command = "settings_set" in {
  def global : Option<"global", "g">, Argument<"Filename">,
    Completion<"DiskFile">,
    Desc<"Apply the new value to the global default value.">;
  def force : Option<"force", "f">,
    Desc<"Force an empty value to be accepted as the default.">;
}

let Command = "settings_write" in {
  def settings_wf : Option<"file", "f">, Required,
    Desc<"The file into which to write the settings.">;
  def append : Option<"append", "a">,
    Desc<"Append to saved settings file if it exists.">;
}

let Command = "settings_read" in {
  def settings_rf : Option<"file", "f">, Required,
  Desc<"The file from which to read the settings.">;
}

let Command = "breakpoint_list" in {
  def internal : Option<"internal", "i">,
    Desc<"Show debugger internal breakpoints">;
  def brief : Option<"brief", "b">, Group<1>,
    Desc<"Give a brief description of the breakpoint (no location info).">;
  def full : Option<"full", "f">, Group<2>,
    Desc<"Give a full description of the breakpoint and its locations.">;
  def verbose : Option<"verbose", "v">, Group<3>,
    Desc<"Explain everything we know about the breakpoint (for debugging "
    "debugger bugs).">;
  def dummy_bp : Option<"dummy-breakpoints", "D">,
    Desc<"List Dummy breakpoints - i.e. breakpoints set before a file is "
    "provided, which prime new targets.">;
}
