#ifndef PLUGIN_H_IN
#define PLUGIN_H_IN

extern "C" {
void *dylib1_init(void);
void *dylib2_init(void);
void *dylib3_init(void);
void *dylib4_init(void);
}

#endif // PLUGIN_H_IN
