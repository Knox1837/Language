#pragma once
// map_methods.h: resolves obj.method for a map
// Returns a NativeFunction already bound to the specific map instance it was looked up on

#include <memory>
#include "value.h"
#include "../lexer/token.h"

class MapObject;

// Throws RuntimeError if `name` isn't a known map method.
Value getMapMethod(std::shared_ptr<MapObject> map, const Token& name);