#ifndef WB_SCENE_H
#define WB_SCENE_H
#include <stdint.h>
typedef struct { float x,y; } wb_scene_vec;
typedef struct { float x0,y0,x1,y1; } wb_scene_rect;
typedef void (*wb_scene_draw_fn)(void *,const wb_scene_vec *,float,float,void *,void *,unsigned char *,const wb_scene_rect *);
typedef void (*wb_scene_pos_fn)(wb_scene_vec *,const void *,int);
int wb_scene_bind(void *,wb_scene_draw_fn,wb_scene_pos_fn);
void wb_scene_draw(void *,const wb_scene_vec *,float,float,void *,void *,unsigned char *,const wb_scene_rect *);
#endif
