#ifndef WB_PLATFORM_H
#define WB_PLATFORM_H
#include "wb_port.h"
const so_default_dynlib *wb_platform_bindings(size_t *);
const so_default_dynlib *wb_locale_bindings(size_t *);
const so_default_dynlib *wb_asset_bindings(size_t *);
void wb_platform_set_module(so_module *);
int wb_platform_init(void);
#endif
