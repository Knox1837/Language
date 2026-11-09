// map_object.h: the runtime representation of a map/dictionary.
#pragma once
#include <map>
#include <string>
#include "value.h"
#include "../util/map_key.h"

class MapObject {
public:
    // Keyed by string, number or boolean
    std::map<MapKey, Value, MapKeyLess> entries;
};