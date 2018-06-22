#include <iostream>
#include <string>
#include <vector>

/// Source content handler for file reading, position reporting and so
/// on.
struct Source {
    // Create from a filepath.
    Source(const std::string &p);

    const std::string path;
    std::vector<char> buf;
};
