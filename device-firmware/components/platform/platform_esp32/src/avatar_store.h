#pragma once

#include <stdbool.h>
#include <stdint.h>

int avatar_store_init(void);
bool avatar_store_has(const char *name);
int avatar_store_first_name(char name[16]);
int avatar_store_name_at(uint8_t index, char name[16]);
int avatar_store_get(const char *name, const uint8_t **png, int *len);
void avatar_store_deinit(void);
