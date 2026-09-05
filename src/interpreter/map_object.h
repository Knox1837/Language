// map_object.h: the runtime representation of a map/dictionary.
#pragma once
#include <map>
#include <string>
#include "value.h"

class MapObject {
public:
    std::map<std::string, Value> entries;
};