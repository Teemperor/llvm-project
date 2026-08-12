#ifndef PLUGIN_H
#define PLUGIN_H

extern "C" {
void plugin_init(void *derived_from_main_ptr);
void plugin_entry();
}

#endif
