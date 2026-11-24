// guide_test.cpp — THE GUIDE SAYS WHAT THE CODE DOES: a file it quotes whole is that file, every link inside it reaches a heading,
// and every name of the library it uses (a function, a type, a constant, a variable) exists, so a rename cannot leave it behind.
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <string>

#include "rolltui_test.hpp"

using namespace testkit;

namespace {

std::string read(const std::filesystem::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// GitHub's anchor for a heading: lower case, punctuation but '-' and '_' dropped, spaces as '-'.
std::string anchor(const std::string& heading) {
  std::string a;
  for (char c : heading) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u >= 'A' && u <= 'Z') a += static_cast<char>(u - 'A' + 'a');
    else if ((u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') || c == '-' || c == '_') a += c;
    else if (c == ' ') a += '-';
    else if (u >= 0x80) a += c;
  }
  return a;
}

}  // namespace

int main() {
  const std::filesystem::path root = ROLLTUI_SOURCE_DIR;
  const std::string guide = read(root / "docs/guide.md");
  check(guide.size() > 10000, "the guide is read");

  // QUOTED WHOLE: the fenced block after each marker is the file, byte for byte
  static const std::regex quoted(R"(<!-- quoted whole: ([^ ]+) -->\n```[a-z]*\n)");
  int quotes = 0;
  for (std::sregex_iterator it(guide.begin(), guide.end(), quoted), end; it != end; ++it) {
    const std::string path = (*it)[1];
    const std::size_t from = static_cast<std::size_t>(it->position() + it->length());
    const std::size_t to = guide.find("\n```\n", from);
    std::string file = read(root / path);
    while (!file.empty() && file.back() == '\n') file.pop_back();
    check(to != std::string::npos && guide.compare(from, to - from, file) == 0, "the guide quotes " + path + " as it is");
    ++quotes;
  }
  check(quotes >= 2, "…the first program and its test among them [" + std::to_string(quotes) + "]");

  // LINKS INSIDE IT reach a heading
  std::set<std::string> headings;
  static const std::regex heading(R"(^#{1,6} (.+)$)", std::regex::multiline);
  for (std::sregex_iterator it(guide.begin(), guide.end(), heading), end; it != end; ++it) headings.insert(anchor((*it)[1]));
  static const std::regex link(R"(\]\(#([^)]+)\))");
  std::string unresolved;
  int links = 0;
  for (std::sregex_iterator it(guide.begin(), guide.end(), link), end; it != end; ++it, ++links)
    if (!headings.count((*it)[1])) unresolved += " #" + std::string((*it)[1]);
  check(links > 30 && unresolved.empty(), "every link inside the guide reaches a heading [" + std::to_string(links) + "]" + unresolved);

  // EVERY NAME IT USES EXISTS: in the public headers, the library's sources, the examples, the tools or the build
  std::string corpus = read(root / "rolltui.h") + read(root / "rolltui_cpp.h") + read(root / "rolltui_studio.h") + read(root / "CMakeLists.txt");
  for (const char* dir : {"c", "examples", "tools"})
    for (const auto& e : std::filesystem::recursive_directory_iterator(root / dir))
      if (e.is_regular_file()) {
        const std::string ext = e.path().extension().string();
        if (ext == ".c" || ext == ".h" || ext == ".cpp" || ext == ".hpp" || ext == ".sh" || ext == ".py" || ext == ".js") corpus += read(e.path());
      }
  static const std::regex name(R"(\b(rolltui_[a-z0-9_]+|Rolltui[A-Za-z0-9_]+|ROLLTUI_[A-Z0-9_]+|ROLL_[A-Z0-9_]+)\b)");
  std::set<std::string> names, missing;
  for (std::sregex_iterator it(guide.begin(), guide.end(), name), end; it != end; ++it) names.insert((*it)[1]);
  for (const std::string& n : names)
    if (corpus.find(n) == std::string::npos) missing.insert(n);
  std::string said;
  for (const std::string& n : missing) said += " " + n;
  check(names.size() > 300 && missing.empty(), "every library name the guide uses exists [" + std::to_string(names.size()) + "]" + said);
  return report("guide_test");
}
