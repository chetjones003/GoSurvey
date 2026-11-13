import importlib.util
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
            (root / "src/commands/CadCommands.hpp").write_text(
                '#include "Peer.hpp"\n#include <QWidget>\n' + ("// header padding\n" * 8), encoding="utf-8")
            (root / "src/commands/Peer.hpp").write_text(
                '#include "CadCommands.hpp"\n#include <GL/gl.h>\n', encoding="utf-8")
            (root / "src/ui.cpp").write_text('#include <QWidget>\n', encoding="utf-8")
            (root / "src/qt/Widget.cpp").write_text('#include <QWidget>\n', encoding="utf-8")
            (root / "src/render/Renderer.cpp").write_text('#include <GL/gl.h>\n', encoding="utf-8")

            result = inventory.collect_metrics(root)

        self.assertEqual(result["largest_files"][0]["path"], "src/commands/CadCommands.hpp")
        self.assertEqual(result["folder_line_counts"]["src/commands"], 12)
        self.assertEqual(result["cad_commands_header_fan_in"], 1)
        self.assertEqual(result["include_graph"]["src/commands/CadCommands.hpp"], ["src/commands/Peer.hpp"])
        self.assertEqual(result["dependency_cycles"], [["src/commands/CadCommands.hpp", "src/commands/Peer.hpp"]])
        self.assertEqual(result["qt_includes_outside_qt"]["files"], ["src/commands/CadCommands.hpp", "src/ui.cpp"])
        self.assertEqual(result["opengl_outside_rendering"]["files"], ["src/commands/Peer.hpp"])

    def test_cli_writes_report_and_json(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "src").mkdir()
            (root / "src/main.cpp").write_text("int main() {}\n", encoding="utf-8")
            out = root / "out"
            report_path, metrics_path = inventory.write_report(root, out)
            self.assertIn("Architecture inventory", report_path.read_text(encoding="utf-8"))
            self.assertEqual(inventory.json.loads(metrics_path.read_text(encoding="utf-8"))["source_file_count"], 1)


if __name__ == "__main__":
    unittest.main()
