# pugixml

- **Upstream:** github.com/zeux/pugixml
- **Pin:** `v1.14`
- **Form:** `src/pugixml.hpp`, `src/pugixml.cpp`, `src/pugiconfig.hpp` — unmodified upstream source, plus `LICENSE.md` (MIT).

Added for GitHub issue #846 (Phase B): the Trimble JobXML importer (`src/traverse/JobXmlImport.cpp`)
is the first GoSurvey feature to need a real XML parser. pugixml was chosen over hand-rolling one
because XML's escaping/CDATA/nesting rules are an easy place to get subtly wrong, and pugixml is a
small, widely-used, permissively-licensed (MIT), two-file (`.hpp`/`.cpp`) library — no other
dependencies, no build-system integration beyond compiling one `.cpp`.

To update: replace `src/pugixml.{hpp,cpp}` and `src/pugiconfig.hpp` with the new tag's versions from
https://github.com/zeux/pugixml/tree/<tag>/src, and bump the pin above.
