#pragma once
#include <stdint.h>
void wb_name_hash_bind(uint64_t (*)(const char *,int));
uint64_t wb_name_hash(const char *,int);
