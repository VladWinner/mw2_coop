#pragma once
#include <stddef.h>
#include <stdint.h>

void memdump(uintptr_t addr, size_t size);
int memdump_command(char *line); // 1 if the line was !asset or !mem
