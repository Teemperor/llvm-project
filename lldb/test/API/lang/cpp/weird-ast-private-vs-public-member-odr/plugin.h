#ifndef PLUGIN_H_IN
#define PLUGIN_H_IN

extern "C" {
void plugin_init(void);
// Takes a pointer to main.cpp's 'Secret' object, passed as 'void *' so that
// this header doesn't need to (and must not) share a single definition of
// 'Secret' between main.cpp and plugin.cpp: each side defines its own
// conflicting 'Secret' independently, which is the whole point of this
// test.
void plugin_entry(void *secretFromMainExe);
}

#endif // PLUGIN_H_IN
