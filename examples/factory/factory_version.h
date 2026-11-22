/**
 * @file      factory_version.h
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-11-22
 *
 * Provides the Factory version and build identity shown on the boot screen.
 */
#ifndef FACTORY_VERSION_H
#define FACTORY_VERSION_H

/* Keep this value stable until the next Factory feature release. */
#ifndef FACTORY_BUILD_VERSION
#define FACTORY_BUILD_VERSION "1.0.0"
#endif

#if defined(__has_include)
#if __has_include("factory_build_info.h")
#include "factory_build_info.h"
#endif
#endif

#ifndef FACTORY_BUILD_COMMIT
#define FACTORY_BUILD_COMMIT "unknown"
#endif

#ifndef FACTORY_BUILD_TIMESTAMP
#define FACTORY_BUILD_TIMESTAMP "unknown"
#endif

#endif /* FACTORY_VERSION_H */
