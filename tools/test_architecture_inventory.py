import importlib.util
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("architecture_inventory.py")
SPEC = importlib.util.spec_from_file_location("architecture_inventory", SCRIPT)
inventory = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(inventory)


class ArchitectureInventoryTests(unittest.TestCase):
    def test_reports_file_size_folder_lines_graph_leaks_and_cycle(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "src/commands").mkdir(parents=True)
            (root / "src/qt").mkdir(parents=True)
            (root / "src/render").mkdir(parents=True)
            (root / "src/util").mkdir()
            (root / "src/base").mkdir()
            (root / "src/ui").mkdir()
            fixtures = {
                "src/commands/CadCommands.hpp": '#include "Peer.hpp"\n#include <QWidget>\n' + ("// header padding\n" * 8),
                "src/commands/Peer.hpp": '#include "CadCommands.hpp"\n#include <GL/gl.h>\n',
                "src/ui.cpp": '#include "CadCommands.hpp"\n#include <QWidget>\n#include <QOpenGLWidget>\nglBindTexture(GL_TEXTURE_2D, 0);\n',
                "src/qt/Widget.cpp": '#include <QWidget>\n',
                "src/render/Renderer.cpp": '#include <GL/gl.h>\n',
                "src/util/Formats.cpp": '/*\n#include "CadCommands.hpp"\n#include <QWidget>\n#include <GL/gl.h>\n*/\nconst char* format = "glTF OpenGL GL_LINES"; // GL_POINTS\n',
                "src/base/Base.hpp": "struct Base {};\n",
                "src/ui/GlOnly.cpp": "int count = 1'000;\nglClear(0);\n",
            }
            fixtures["src/qt/Widget.cpp"] = '#include <QWidget>\n#include "Base.hpp"\n'
            for relative_path, content in fixtures.items():
                (root / relative_path).write_bytes(content.encode("utf-8"))

            result = inventory.collect_metrics(root)

        ordered_paths = sorted(fixtures, key=lambda path: (-len(fixtures[path].encode()), path))
        self.assertEqual(
            result["largest_files"],
            [{"path": path, "bytes": len(fixtures[path].encode()), "lines": len(fixtures[path].splitlines())}
             for path in ordered_paths],
        )
        self.assertEqual(result["folder_line_counts"], {
            "src": 4, "src/base": 1, "src/commands": 12, "src/qt": 2, "src/render": 1,
            "src/ui": 2, "src/util": 6,
        })
        self.assertEqual(result["cad_commands_header_fan_in"], 2)
        self.assertEqual(result["include_graph"], {
            "src/commands/CadCommands.hpp": ["src/commands/Peer.hpp"],
            "src/commands/Peer.hpp": ["src/commands/CadCommands.hpp"],
            "src/qt/Widget.cpp": ["src/base/Base.hpp"],
            "src/base/Base.hpp": [],
            "src/render/Renderer.cpp": [],
            "src/ui.cpp": ["src/commands/CadCommands.hpp"],
            "src/ui/GlOnly.cpp": [],
            "src/util/Formats.cpp": [],
        })
        self.assertEqual(result["dependency_cycles"], [["src/commands/CadCommands.hpp", "src/commands/Peer.hpp"]])
        self.assertEqual(result["qt_includes_outside_qt"]["files"], ["src/commands/CadCommands.hpp", "src/ui.cpp"])
        self.assertEqual(result["opengl_outside_rendering"]["files"], [
            "src/commands/Peer.hpp", "src/ui.cpp", "src/ui/GlOnly.cpp",
        ])

    def test_cli_writes_report_and_json(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "src").mkdir()
            (root / "src/main.cpp").write_text("int main() {}\n", encoding="utf-8")
            out = root / "out"
            result = subprocess.run(
                [sys.executable, "-B", str(SCRIPT), "--root", str(root), "--output-dir", str(out)],
                check=True, capture_output=True, text=True,
            )
            report_path = out / "architecture-inventory.txt"
            metrics_path = out / "architecture-inventory.json"
            self.assertIn("Architecture inventory", result.stdout)
            self.assertIn("Architecture inventory", report_path.read_text(encoding="utf-8"))
            self.assertEqual(inventory.json.loads(metrics_path.read_text(encoding="utf-8"))["source_file_count"], 1)


if __name__ == "__main__":
    unittest.main()
