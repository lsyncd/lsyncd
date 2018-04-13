/*
| feature.h from Lsyncd -- the Live (Mirror) Syncing Demon
|
| Some Definitions to enable proper clib header features
| Also loads the cmake config file
|
| License: GPLv2 (see COPYING) or any later version
| Authors: Axel Kittenberger <axkibe@gmail.com>
*/
#ifndef FEATURE_H
#define FEATURE_H

#include "config.h"

// some older machines need this to see pselect
#define _DEFAULT_SOURCE 1
#define _XOPEN_SOURCE 700
#define _DARWIN_C_SOURCE 1

#endif
