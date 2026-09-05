// map_lib.h: map/dictionary built-ins: keys, values, hasKey, remove.
#pragma once

#include <memory>
#include "../interpreter/environment.h"

void registerMapLib(std::shared_ptr<Environment> globals);