/*
  Minimal project metadata for app name/version.
  Keeps Main.cpp from needing the full JuceHeader.h.
  Sync with CMakeLists.txt PRODUCT_NAME / VERSION if you change them there.
*/
#pragma once

namespace ProjectInfo {
const char* const projectName   = "Patchworld Bridge";
const char* const companyName   = "Patchworld";
const char* const versionString = "2.0.0";
const int versionNumber         = 0x20000;
}
