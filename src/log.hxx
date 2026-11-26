#pragma once

#include <string>

namespace pluginhost
{
// Appends one line "time|module|event|data" to the file named by the
// PLUGINHOST_LOG environment variable. Does nothing if the variable is unset.
// Never throws.
void logEvent(const char* module, const char* event, const std::string& data = std::string());
}
