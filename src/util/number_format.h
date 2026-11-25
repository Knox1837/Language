// number_format.h: how a number becomes text. Both engines call formatNumber() (from stringifyValue and stringifyVMValue), so print, str(), arrays, maps, error messages and string conversion cannot differ between them.
//
// The text is the SHORTEST one that reads back as exactly the same double (3.14 -> "3.14", 1/3 -> "0.3333333333333333", 0.1 + 0.2 -> "0.30000000000000004"). The layout follows JavaScript: plain decimals for magnitudes from 1e-6 up to just below 1e21, scientific notation outside that range ("1e-7", "2.5e-8", "1e+21"). Whole numbers in range have no decimal point ("100"). Special values: -0 prints "0" (as before), infinity "inf" / "-inf", NaN "nan".
#pragma once
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <string>

inline std::string formatNumber(double d) {
    if (std::isnan(d)) return "nan";
    if (std::isinf(d)) return d > 0 ? "inf" : "-inf";
    if (d == 0) return "0";

    // Shortest round-trip digits in scientific form, e.g. "3.14e+00", "1e+21", "2.5e-08".
    char buf[64];
    auto result = std::to_chars(buf, buf + sizeof buf, std::fabs(d), std::chars_format::scientific);
    std::string sci(buf, result.ptr);

    size_t ePos = sci.find('e');
    std::string digits;
    for (size_t i = 0; i < ePos; i++) {
        if (sci[i] != '.') digits += sci[i];
    }
    int exponent = std::atoi(sci.c_str() + ePos + 1);
    int k = static_cast<int>(digits.size());
    int n = exponent + 1; // the decimal point sits after the first n digits (n <= 0: before all of them)

    std::string out = d < 0 ? "-" : "";
    if (k <= n && n <= 21) {
        out += digits + std::string(n - k, '0');                            // 100, 1e20 as 100000000000000000000
    } else if (0 < n && n <= 21) {
        out += digits.substr(0, n) + "." + digits.substr(n);                // 3.14
    } else if (-6 < n && n <= 0) {
        out += "0." + std::string(-n, '0') + digits;                        // 0.001
    } else {
        out += digits.substr(0, 1);                                         // scientific: 2.5e-8, 1e+21
        if (k > 1) out += "." + digits.substr(1);
        out += "e";
        out += (n - 1 < 0) ? "-" : "+";
        out += std::to_string(std::abs(n - 1));
    }
    return out;
}