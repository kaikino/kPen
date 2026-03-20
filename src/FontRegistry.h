#pragma once

#include <string>
#include <vector>

// FontRegistry scans the system fonts and returns a list of font paths (.ttf/.otf files).

std::vector<std::string> FontRegistry_scanSystemFonts(const std::vector<std::string>& extraDirs = {});
