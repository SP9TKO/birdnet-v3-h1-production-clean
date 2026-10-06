#pragma once
#include <stddef.h>
void diagnostic_dump(const char *name,const void *data,size_t size);
void diagnostic_exit(int status) __attribute__((noreturn));
void diagnostic_read_exact(const char *name,void *data,size_t size);
