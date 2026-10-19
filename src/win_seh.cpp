#include "win_seh.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <csetjmp>

namespace {
thread_local jmp_buf jump;
thread_local unsigned long lastCode = 0;
thread_local bool armed = false;

LONG CALLBACK filter(EXCEPTION_POINTERS* info) {
  if (!armed || !info || !info->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
  const DWORD code = info->ExceptionRecord->ExceptionCode;
  if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_INT_DIVIDE_BY_ZERO &&
      code != EXCEPTION_STACK_OVERFLOW) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  lastCode = code;
  armed = false;
  longjmp(jump, 1);
  return EXCEPTION_CONTINUE_SEARCH;
}
}  // namespace

extern "C" int helios_protect(int (*fn)(void*), void* arg, unsigned long* code) {
  if (!fn) return -1;
  PVOID handle = AddVectoredExceptionHandler(1, filter);
  armed = true;
  lastCode = 0;
  int result = -1;
  if (setjmp(jump) == 0) {
    result = fn(arg);
  } else {
    result = -1;
    if (code) *code = lastCode;
  }
  armed = false;
  if (handle) RemoveVectoredExceptionHandler(handle);
  return result;
}
