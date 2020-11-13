#ifndef H1
#define H1

struct GlobalStruct { int i = 1; };

namespace NS {
  struct NSStruct { int i = 2; };

  inline namespace Inline {
    struct InlineNSStruct { int i = 3; };
    template<typename T>
    struct InlineNSTemplateStruct {
      NSStruct ns;
      T field;
      InlineNSStruct inline_ns;
      typedef GlobalStruct Type;
      Type typedefd;
    };

    template<typename T>
    struct InlineNSTemplateStructWithSpez {
      T i;
      typedef GlobalStruct Type;
      Type typedefd;
    };
    template<>
    struct InlineNSTemplateStructWithSpez<GlobalStruct> {
      long i = 4;
      typedef GlobalStruct Type;
      Type typedefd;
    };
  }

  template<typename T>
  struct NSTemplateStruct {
    NSStruct ns;
    T field;
    InlineNSStruct inline_ns;
    typedef GlobalStruct Type;
    Type typedefd;
  };

  template<typename T>
  struct NSTemplateStructWithSpez {
    T t;
    typedef GlobalStruct Type;
    Type typedefd;
  };
  template<>
  struct NSTemplateStructWithSpez<GlobalStruct> {
    long i = 5;
    typedef GlobalStruct Type;
    Type typedefd;
  };
};

#endif
