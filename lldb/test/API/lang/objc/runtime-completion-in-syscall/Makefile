OBJCXX_SOURCES := main.mm
OBJC_SOURCES := HiddenClass.m

include Makefile.rules

# Only ObjC metadata, no debug info, for HiddenClass.m: its ivars must come
# from the ObjC runtime, exercising TypeSystemClike::GetRuntimeCompletedObjCType.
HiddenClass.o: CFLAGS_EXTRAS += -g0
