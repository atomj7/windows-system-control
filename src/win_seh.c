#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int helios_protect(int (*fn)(void*), void* arg, unsigned long* code) {
  if (!fn) return -1;
  __try {
    return fn(arg);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    if (code) *code = GetExceptionCode();
    return -1;
  }
}
