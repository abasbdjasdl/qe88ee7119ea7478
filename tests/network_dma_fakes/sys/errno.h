#pragma once
#ifdef _WIN32
#include <errno.h>
#else
#include_next <sys/errno.h>
#endif
