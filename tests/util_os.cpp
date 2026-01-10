#include "util_os.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

int get_current_process_id() {
#ifdef _WIN32
  return GetCurrentProcessId();
#else
  return getpid();
#endif
}