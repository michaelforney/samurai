#include "platform.h"

#if PLATFORM_WINDOWS
#  include "platform_windows.c"
#elif PLATFORM_UNIX
#  include "platform_unix.c"
#else
#  error new platform
#endif
