// string_lib.h: string manipulation built-ins: len, upper, lower, substring, and str (convert any value to its string form).
#pragma once
#include <memory>
#include "../interpreter/environment.h"

void registerStringLib(std::shared_ptr<Environment> globals);