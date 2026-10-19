#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int helios_protect(int (*fn)(void*), void* arg, unsigned long* code);

#ifdef __cplusplus
}
#endif
