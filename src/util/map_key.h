// map_key.h: what a map is keyed by, shared by the tree-walker (MapObject) and the VM (VMMap) so both engines accept the same keys, order them the same way and say the same things about bad ones.
// A key is a string, a number or a boolean
#pragma once
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <variant>

struct MapKey {
    // index() doubles as the type's rank in the ordering: 0 bool, 1 number, 2 string.
    std::variant<bool, double, std::string> value;

    // explicit, and none from const char*: a string literal would silently become a bool.
    explicit MapKey(bool b) : value(b) {}
    explicit MapKey(double d) : value(d) {}
    explicit MapKey(std::string s) : value(std::move(s)) {}
    MapKey(const char*) = delete;
};

// The ordering, written to also compare a stored key against a bare bool, number or string. That is what lets `map.find(someString)` look an entry up without first copying the string into a MapKey (a transparent comparator, C++14), so string-keyed maps cost what they did before this type existed.
struct MapKeyLess {
    using is_transparent = void;

    bool operator()(const MapKey& a, const MapKey& b) const {
        size_t ra = a.value.index(), rb = b.value.index();
        if (ra != rb) return ra < rb;
        switch (ra) {
            case 0: return std::get<bool>(a.value) < std::get<bool>(b.value);
            case 1: return std::get<double>(a.value) < std::get<double>(b.value);
            default: return std::get<std::string>(a.value) < std::get<std::string>(b.value);
        }
    }

    bool operator()(const MapKey& a, bool b) const { return a.value.index() == 0 && std::get<bool>(a.value) < b; }
    bool operator()(bool a, const MapKey& b) const { return b.value.index() != 0 || a < std::get<bool>(b.value); }

    bool operator()(const MapKey& a, double b) const {
        size_t r = a.value.index();
        if (r != 1) return r < 1; // a bool is below every number, a string above
        return std::get<double>(a.value) < b;
    }
    bool operator()(double a, const MapKey& b) const {
        size_t r = b.value.index();
        if (r != 1) return r > 1;
        return a < std::get<double>(b.value);
    }

    bool operator()(const MapKey& a, const std::string& b) const { return a.value.index() < 2 || std::get<std::string>(a.value) < b; }
    bool operator()(const std::string& a, const MapKey& b) const { return b.value.index() == 2 && a < std::get<std::string>(b.value); }
};

// Whether a script value may be used as a key. `V` is Value in the tree-walker and VMValue in the VM: different variants, but both hold bool, double and std::string.
enum class KeyCheck { OK, BAD_TYPE, IS_NAN };

template <class V>
KeyCheck checkKey(const V& v) {
    if (std::holds_alternative<std::string>(v) || std::holds_alternative<bool>(v)) return KeyCheck::OK;
    if (const double* d = std::get_if<double>(&v)) return std::isnan(*d) ? KeyCheck::IS_NAN : KeyCheck::OK;
    return KeyCheck::BAD_TYPE;
}

// The message a script sees for a key that failed checkKey.
inline const char* keyProblem(KeyCheck check) {
    return check == KeyCheck::IS_NAN ? "Map key can't be NaN." : "Map keys must be strings, numbers or booleans.";
}

// The functions below require checkKey(key) == OK.
template <class V>
MapKey makeKey(const V& key) {
    if (const std::string* s = std::get_if<std::string>(&key)) return MapKey(*s);
    if (const double* d = std::get_if<double>(&key)) return MapKey(*d);
    return MapKey(std::get<bool>(key));
}

template <class V>
V keyToValue(const MapKey& key) {
    return std::visit([](const auto& x) { return V(x); }, key.value);
}

template <class Map, class V>
auto findKey(Map& map, const V& key) {
    if (const std::string* s = std::get_if<std::string>(&key)) return map.find(*s);
    if (const double* d = std::get_if<double>(&key)) return map.find(*d);
    return map.find(std::get<bool>(key));
}

// Sets map[key] = mapped, inserting if the key is new. Looks up by the bare value first, so overwriting an existing entry copies nothing.
template <class Map, class V, class Mapped>
void putKey(Map& map, const V& key, Mapped&& mapped) {
    auto it = findKey(map, key);
    if (it != map.end()) {
        it->second = std::forward<Mapped>(mapped);
    } else {
        map.emplace(makeKey(key), std::forward<Mapped>(mapped));
    }
}

template <class Map, class V>
bool hasKey(Map& map, const V& key) { return findKey(map, key) != map.end(); }

template <class Map, class V>
bool eraseKey(Map& map, const V& key) {
    auto it = findKey(map, key);
    if (it == map.end()) return false;
    map.erase(it);
    return true;
}