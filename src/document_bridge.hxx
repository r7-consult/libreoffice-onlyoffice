#pragma once

// Methods reached through executeMethod (stage 4). Each returns a JSON text
// for the result, or throws std::runtime_error with a message for the page.
// Writer only in stage 4: GetSelectedText, PasteText.

#include <string>

namespace pluginhost
{
// argsJson is a JSON array text, e.g. ["text"].
std::string callDocumentMethod(const std::string& name, const std::string& argsJson);
}
