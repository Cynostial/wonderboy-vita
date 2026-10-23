#ifndef WB_PATHS_H
#define WB_PATHS_H
#include <stddef.h>
#define WB_PATH_MAX 1024
#define WB_ROOT "ux0:data/wonderboy"
#define WB_ASSETS WB_ROOT "/assets"
#define WB_SAVES WB_ROOT "/saves"
void wb_paths_imported(int enabled);
int wb_path_resolve(const char *,char *,size_t);
#endif
