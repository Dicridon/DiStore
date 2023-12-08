#ifndef __DISTORE__DEBUG__DEBUG__
#define __DISTORE__DEBUG__DEBUG__

#include "config/config.hpp"
#include <cstdio>
#include <cstdarg>
namespace DiStore::Debug {
    auto info(const char *fmt, ...) -> void;
    auto warn(const char *fmt, ...) -> void;
    auto error(const char *fmt, ...) -> void;
}
#endif
