// file_io.h: the file and folder operations behind readFile, writeFile, appendFile, readLines, fileExists, deleteFile, isFile, isDir, listDir, makeDir, removeDir, renameFile and copyFile. Header-only, and shared by the tree-walker (src/stdlib/io_lib.cpp) and the VM (src/vm/vm_stdlib.cpp), so both engines behave the same and say the same thing when something goes wrong.
// Files are read and written as raw bytes (binary mode), matching a script string, which is a sequence of bytes. Nothing is translated: on Windows a "\n" in a string is written as one byte, not "\r\n", and "\r\n" in a file is read back as two bytes. readLines is the one place line endings are interpreted, and it accepts both.
// Every function reports failure by returning false and filling `error` with the message the script sees; none throws.
#pragma once
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace fileio {

// Reads the whole file into `out`. A directory counts as "could not open", the same on every platform (an ifstream may open one without complaint on some systems).
inline bool readAll(const std::string& path, std::string& out, std::string& error) {
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) {
        error = "Could not open file '" + path + "' for reading.";
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "Could not open file '" + path + "' for reading.";
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf(); // (inserting an empty file sets failbit on `buffer`; harmless, str() is just "")
    if (file.bad()) {
        error = "Could not read file '" + path + "'.";
        return false;
    }
    out = buffer.str();
    return true;
}

// Writes `text` to the file, replacing its contents (or adding to the end, if `append`). Creates the file if it does not exist, but not the folders above it.
inline bool writeAll(const std::string& path, const std::string& text, bool append, std::string& error) {
    std::ofstream file(path, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
    if (!file) {
        error = "Could not open file '" + path + "' for " + (append ? "appending" : "writing") + ".";
        return false;
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.flush();
    if (!file) {
        error = "Could not write to file '" + path + "'.";
        return false;
    }
    return true;
}

// Splits text into lines: "\n" ends a line, and a "\r" right before it is dropped, so Unix and Windows files read alike. A final line with no newline still counts; a trailing newline does not add an empty one. So "a\nb\n" and "a\nb" are both [a, b], "" is [], "\n" is [""], and "a\n\nb" is [a, "", b].
inline std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        size_t stop = (end == std::string::npos) ? text.size() : end;
        size_t contentEnd = stop;
        if (contentEnd > start && text[contentEnd - 1] == '\r') contentEnd--;
        lines.push_back(text.substr(start, contentEnd - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return lines;
}

// True if something (a file or a folder) exists at the path.
inline bool exists(const std::string& path) {
    std::error_code ec;
    bool found = std::filesystem::exists(path, ec);
    return found && !ec;
}

// Deletes a file. `removed` says whether there was one: a path that does not exist is not an error. A folder is never deleted.
inline bool removeFile(const std::string& path, bool& removed, std::string& error) {
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) {
        error = "Could not delete '" + path + "': it is a folder.";
        return false;
    }
    removed = std::filesystem::remove(path, ec);
    if (ec) {
        error = "Could not delete file '" + path + "'.";
        return false;
    }
    return true;
}

// True only for a regular file / only for a folder (fileExists is true for either).
inline bool isFile(const std::string& path) {
    std::error_code ec;
    bool yes = std::filesystem::is_regular_file(path, ec);
    return yes && !ec;
}
inline bool isDir(const std::string& path) {
    std::error_code ec;
    bool yes = std::filesystem::is_directory(path, ec);
    return yes && !ec;
}

// The names (not paths) of what is in a folder, files and folders alike, without "." and "..", sorted bytewise so the order is the same on every platform and in both engines.
inline bool listDir(const std::string& path, std::vector<std::string>& names, std::string& error) {
    std::error_code ec;
    if (!std::filesystem::is_directory(path, ec) || ec) {
        error = "Could not list folder '" + path + "'.";
        return false;
    }
    names.clear();
    for (std::filesystem::directory_iterator it(path, ec), end; !ec && it != end; it.increment(ec)) {
        names.push_back(it->path().filename().string());
    }
    if (ec) {
        error = "Could not list folder '" + path + "'.";
        names.clear();
        return false;
    }
    std::sort(names.begin(), names.end());
    return true;
}

// Creates a folder, and any missing folders above it. `created` says whether it had to: a folder that is already there is not an error. A file in the way is.
inline bool makeDir(const std::string& rawPath, bool& created, std::string& error) {
    std::string path = rawPath;
    while (path.size() > 1 && (path.back() == '/' || path.back() == '\\')) path.pop_back(); // "a/b/" is "a/b"
    std::error_code ec;
    if (path.empty()) {
        error = "Could not create folder '" + rawPath + "'.";
        return false;
    }
    if (std::filesystem::is_directory(path, ec)) {
        created = false;
        return true;
    }
    if (std::filesystem::exists(path, ec)) {
        error = "Could not create folder '" + rawPath + "': a file is in the way.";
        return false;
    }
    std::filesystem::create_directories(path, ec);
    if (ec || !std::filesystem::is_directory(path, ec)) {
        error = "Could not create folder '" + rawPath + "'.";
        return false;
    }
    created = true;
    return true;
}

// Removes an EMPTY folder. `removed` says whether there was one: a path that does not exist is not an error. A file, or a folder with anything in it, is.
inline bool removeDir(const std::string& path, bool& removed, std::string& error) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        removed = false;
        return true;
    }
    if (!std::filesystem::is_directory(path, ec)) {
        error = "Could not remove folder '" + path + "': it is a file.";
        return false;
    }
    if (std::filesystem::directory_iterator(path, ec) != std::filesystem::directory_iterator()) {
        error = "Could not remove folder '" + path + "': it is not empty.";
        return false;
    }
    removed = std::filesystem::remove(path, ec);
    if (ec) {
        error = "Could not remove folder '" + path + "'.";
        return false;
    }
    return true;
}

// Renames or moves a file or a folder. It never replaces something that is already at the new path (the platforms disagree about whether it would, and a script can deleteFile first if it means to).
inline bool renameEntry(const std::string& from, const std::string& to, std::string& error) {
    std::error_code ec;
    std::string base = "Could not rename '" + from + "' to '" + to + "'";
    if (!std::filesystem::exists(from, ec)) {
        error = base + ": '" + from + "' does not exist.";
        return false;
    }
    if (std::filesystem::exists(to, ec)) {
        error = base + ": '" + to + "' already exists.";
        return false;
    }
    std::filesystem::rename(from, to, ec);
    if (ec) {
        error = base + ".";
        return false;
    }
    return true;
}

// Copies one file (not a folder). Like renameEntry, it never replaces an existing path.
inline bool copyFile(const std::string& from, const std::string& to, std::string& error) {
    std::error_code ec;
    std::string base = "Could not copy '" + from + "' to '" + to + "'";
    if (!std::filesystem::is_regular_file(from, ec)) {
        error = base + ": '" + from + "' is not a file.";
        return false;
    }
    if (std::filesystem::exists(to, ec)) {
        error = base + ": '" + to + "' already exists.";
        return false;
    }
    std::filesystem::copy_file(from, to, ec);
    if (ec) {
        error = base + ".";
        return false;
    }
    return true;
}

} // namespace fileio