#include "PdfRaw.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace pdfview::raw {

namespace {

bool IsWs(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == '\0'; }
bool IsDigit(char c) { return c >= '0' && c <= '9'; }

size_t SkipWs(const std::string& s, size_t i) {
  while (i < s.size() && IsWs(s[i]))
    ++i;
  return i;
}

} // namespace

bool ParseRef(const std::string& s, size_t i, int& num, size_t& after) {
  i = SkipWs(s, i);
  size_t a = i;
  while (i < s.size() && IsDigit(s[i]))
    ++i;
  if (i == a || i - a > 9)
    return false;
  num = std::atoi(s.substr(a, i - a).c_str());
  size_t j = SkipWs(s, i);
  if (j == i)
    return false;
  size_t g = j;
  while (j < s.size() && IsDigit(s[j]))
    ++j;
  if (j == g)
    return false;
  size_t k = SkipWs(s, j);
  if (k == j || k >= s.size() || s[k] != 'R')
    return false;
  if (k + 1 < s.size() && (std::isalnum(static_cast<unsigned char>(s[k + 1])) != 0))
    return false;
  after = k + 1;
  return true;
}

size_t FindKey(const std::string& s, const char* key, size_t from) {
  const std::string k = key;
  for (size_t at = s.find(k, from); at != std::string::npos; at = s.find(k, at + 1)) {
    const size_t e = at + k.size();
    if (e >= s.size() || !(std::isalnum(static_cast<unsigned char>(s[e])) != 0)) // "/Pages" is not "/Page"
      return SkipWs(s, e);
  }
  return std::string::npos;
}

size_t SkipValue(const std::string& s, size_t pos) {
  int depth = 0;
  size_t i = pos;
  while (i < s.size()) {
    const char c = s[i];
    if (c == '(') { // a literal string, with nesting and backslash escapes
      int d = 1;
      ++i;
      while (i < s.size() && d > 0) {
        if (s[i] == '\\')
          ++i;
        else if (s[i] == '(')
          ++d;
        else if (s[i] == ')')
          --d;
        ++i;
      }
      continue;
    }
    if (c == '<' && i + 1 < s.size() && s[i + 1] == '<') {
      ++depth;
      i += 2;
      continue;
    }
    if (c == '>' && i + 1 < s.size() && s[i + 1] == '>') {
      --depth;
      i += 2;
      if (depth <= 0)
        return i;
      continue;
    }
    if (c == '<') { // a hex string
      while (i < s.size() && s[i] != '>')
        ++i;
      ++i;
      continue;
    }
    if (c == '[') {
      ++depth;
      ++i;
      continue;
    }
    if (c == ']') {
      --depth;
      ++i;
      if (depth <= 0)
        return i;
      continue;
    }
    ++i;
  }
  return std::string::npos;
}

std::map<int, Span> ScanObjects(const std::string& b) {
  std::map<int, Span> out;
  size_t pos = 0;
  while ((pos = b.find(" obj", pos)) != std::string::npos) {
    const size_t after = pos + 4;
    const size_t gEnd = pos;
    size_t g0 = gEnd;
    while (g0 > 0 && IsDigit(b[g0 - 1]))
      --g0;
    if (g0 == gEnd || g0 == 0 || b[g0 - 1] != ' ' || (after < b.size() && std::isalnum(static_cast<unsigned char>(b[after])) != 0)) {
      pos = after;
      continue;
    }
    const size_t nEnd = g0 - 1;
    size_t n0 = nEnd;
    while (n0 > 0 && IsDigit(b[n0 - 1]))
      --n0;
    if (n0 == nEnd || nEnd - n0 > 9 || (n0 > 0 && b[n0 - 1] != '\n' && b[n0 - 1] != '\r')) {
      pos = after;
      continue;
    }
    // A real object header follows "endobj", the file header or a comment line; not bytes inside a stream.
    size_t k = n0;
    while (k > 0 && IsWs(b[k - 1]))
      --k;
    bool ok = k < 64 || (k >= 6 && b.compare(k - 6, 6, "endobj") == 0) || (k >= 5 && b.compare(k - 5, 5, "%%EOF") == 0);
    if (!ok) {
      size_t ls = k;
      while (ls > 0 && b[ls - 1] != '\n' && b[ls - 1] != '\r')
        --ls;
      ok = ls < b.size() && b[ls] == '%';
    }
    if (!ok) {
      pos = after;
      continue;
    }
    const size_t end = b.find("endobj", after);
    if (end == std::string::npos)
      break;
    Span sp;
    sp.start = n0;
    sp.body = after;
    sp.end = end;
    out[std::atoi(b.substr(n0, nEnd - n0).c_str())] = sp;
    pos = end + 6;
  }
  return out;
}

std::string Body(const std::string& bytes, const std::map<int, Span>& objs, int num) {
  const auto it = objs.find(num);
  if (it == objs.end())
    return {};
  return bytes.substr(it->second.body, it->second.end - it->second.body);
}

std::string Expand(const std::string& bytes, const std::map<int, Span>& objs, const std::string& text, int depth) {
  if (depth <= 0)
    return text;
  std::string out;
  size_t i = 0;
  while (i < text.size()) {
    // A reference starts at a digit that is not the tail of a longer token.
    if (IsDigit(text[i]) && (i == 0 || !(std::isalnum(static_cast<unsigned char>(text[i - 1])) != 0 || text[i - 1] == '.'))) {
      int num = 0;
      size_t after = 0;
      if (ParseRef(text, i, num, after) && objs.count(num) != 0) {
        out += ' ';
        out += Expand(bytes, objs, Body(bytes, objs, num), depth - 1);
        out += ' ';
        i = after;
        continue;
      }
    }
    if (text[i] == '(') { // keep string contents as they are
      int d = 1;
      size_t j = i + 1;
      while (j < text.size() && d > 0) {
        if (text[j] == '\\')
          ++j;
        else if (text[j] == '(')
          ++d;
        else if (text[j] == ')')
          --d;
        ++j;
      }
      out += text.substr(i, j - i);
      i = j;
      continue;
    }
    out += text[i++];
  }
  return out;
}

bool PageObjects(const std::string& bytes, const std::map<int, Span>& objs, std::vector<int>& pages, std::string& why) {
  pages.clear();
  const size_t rootAt = bytes.rfind("/Root");
  int root = 0;
  size_t after = 0;
  if (rootAt == std::string::npos || !ParseRef(bytes, rootAt + 5, root, after)) {
    why = "the file has no document catalog";
    return false;
  }
  const std::string cat = Body(bytes, objs, root);
  const size_t pk = FindKey(cat, "/Pages");
  int top = 0;
  if (pk == std::string::npos || !ParseRef(cat, pk, top, after)) {
    why = "the document catalog has no page tree";
    return false;
  }
  std::set<int> seen;
  struct Walk {
    const std::string& bytes;
    const std::map<int, Span>& objs;
    std::set<int>& seen;
    std::vector<int>& pages;
    bool Go(int node, int depth) {
      if (depth > 64 || !seen.insert(node).second)
        return false;
      const std::string body = Body(bytes, objs, node);
      if (body.empty())
        return false;
      const size_t kk = FindKey(body, "/Kids");
      if (kk == std::string::npos) {
        pages.push_back(node);
        return true;
      }
      std::string arr;
      if (kk < body.size() && body[kk] == '[') {
        const size_t e = SkipValue(body, kk);
        if (e == std::string::npos)
          return false;
        arr = body.substr(kk, e - kk);
      } else {
        int ref = 0;
        size_t af = 0;
        if (!ParseRef(body, kk, ref, af))
          return false;
        arr = Body(bytes, objs, ref);
      }
      size_t i = 0;
      while (i < arr.size()) {
        int kid = 0;
        size_t af = 0;
        if (IsDigit(arr[i]) && ParseRef(arr, i, kid, af)) {
          if (!Go(kid, depth + 1))
            return false;
          i = af;
        } else {
          ++i;
        }
      }
      return true;
    }
  } walk{bytes, objs, seen, pages};
  if (!walk.Go(top, 0)) {
    why = "the page tree could not be followed";
    return false;
  }
  return true;
}

std::string AppendXref(const std::string& bytes) {
  const std::map<int, Span> objs = ScanObjects(bytes);
  if (objs.empty())
    return {};
  // The trailer entries to keep: from a classic trailer, or from the cross-reference stream's dictionary.
  std::string dict;
  const size_t tr = bytes.rfind("trailer");
  if (tr != std::string::npos) {
    const size_t d = bytes.find("<<", tr);
    const size_t e = d == std::string::npos ? std::string::npos : SkipValue(bytes, d);
    if (e != std::string::npos)
      dict = bytes.substr(d, e - d);
  }
  if (dict.empty()) {
    for (auto it = objs.rbegin(); it != objs.rend() && dict.empty(); ++it) {
      const std::string body = Body(bytes, objs, it->first);
      const size_t t = body.find("/XRef");
      const size_t d = body.find("<<");
      if (t != std::string::npos && d != std::string::npos) {
        const size_t e = SkipValue(body, d);
        if (e != std::string::npos)
          dict = body.substr(d, e - d);
      }
    }
  }
  if (dict.empty() || dict.find("/Encrypt") != std::string::npos)
    return {};
  std::string trailer = "<<";
  int maxNum = objs.rbegin()->first;
  trailer += "/Size " + std::to_string(maxNum + 1);
  for (const char* key : {"/Root", "/Info"}) {
    const size_t at = FindKey(dict, key);
    int num = 0;
    size_t after = 0;
    if (at != std::string::npos && ParseRef(dict, at, num, after))
      trailer += std::string(key) + " " + std::to_string(num) + " 0 R";
  }
  const size_t idAt = FindKey(dict, "/ID");
  if (idAt != std::string::npos && idAt < dict.size() && dict[idAt] == '[') {
    const size_t e = SkipValue(dict, idAt);
    if (e != std::string::npos)
      trailer += "/ID" + dict.substr(idAt, e - idAt);
  }
  trailer += ">>";
  if (trailer.find("/Root") == std::string::npos)
    return {};

  std::string out = bytes;
  if (out.empty() || out.back() != '\n')
    out += '\n';
  const size_t xrefAt = out.size();
  out += "xref\n0 " + std::to_string(maxNum + 1) + "\n";
  char line[32];
  for (int n = 0; n <= maxNum; ++n) {
    const auto it = objs.find(n);
    if (n == 0)
      std::snprintf(line, sizeof(line), "0000000000 65535 f \n");
    else if (it == objs.end())
      std::snprintf(line, sizeof(line), "0000000000 00000 f \n");
    else
      std::snprintf(line, sizeof(line), "%010zu 00000 n \n", it->second.start);
    out += line;
  }
  out += "trailer\n" + trailer + "\nstartxref\n" + std::to_string(xrefAt) + "\n%%EOF\n";
  return out;
}

} // namespace pdfview::raw
