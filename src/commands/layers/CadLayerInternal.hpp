#include <string>

static bool ValidNewLayerNameChars(const std::string& n) {
  if (n.empty() || n.size() > 255)
    return false;
  for (unsigned char c : n) {
    if (c < 32 || c == 127)
      return false;
    if (c == '<' || c == '>' || c == '/' || c == '\\' || c == '"' || c == ':' || c == ';' || c == '?' ||
        c == '*' || c == '|' || c == ',' || c == '`')
      return false;
  }
  return true;
}
