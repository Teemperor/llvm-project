#include "h4.h"
#include <iostream>

int main() {
  GlobalStruct s;
  NS::NSStruct ns;
  NS::InlineNSStruct ins;

  NS::InlineNSTemplateStruct<int> inline_template;
  inline_template.field = 6;
  NS::InlineNSTemplateStructWithSpez<GlobalStruct> inline_spez;
  NS::InlineNSTemplateStructWithSpez<int> inline_spez_int;

  NS::NSTemplateStruct<int> template_s;
  template_s.field = 7;
  NS::NSTemplateStructWithSpez<GlobalStruct> spez;
  NS::NSTemplateStructWithSpez<int> spez_int;

  return template_s.field; // break here
}
