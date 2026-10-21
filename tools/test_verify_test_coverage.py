import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('coverage', Path(__file__).with_name('verify-test-coverage.py'))
coverage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(coverage)


class CoverageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def fixture(self, kind='catch'):
        for index, name in enumerate(['first', 'second']):
            shard = self.root / f'shard-{index}'
            shard.mkdir()
            (shard / 'discovery.json').write_text(json.dumps({
                'index': index, 'count': 2, 'expected': ['first', 'second'], 'selected': [name]}))
            if kind == 'catch':
                (shard / 'results.xml').write_text(f'<Catch2TestRun><TestCase name="{name}">'
                    '<OverallResult success="true"/></TestCase><OverallResultsCases/></Catch2TestRun>')
            else:
                (shard / 'artifacts').mkdir()
                (shard / 'artifacts/qt-tests.xml').write_text(f'<testsuite><testcase name="{name}"/></testsuite>')

    def test_complete_catch_coverage(self):
        self.fixture()
        self.assertEqual(coverage.verify_group(self.root, 'catch')['cases'], 2)

    def test_duplicate_and_missing_case_fails(self):
        self.fixture()
        path = self.root / 'shard-1/results.xml'
        path.write_text(path.read_text().replace('second', 'first'))
        with self.assertRaisesRegex(ValueError, 'missing='):
            coverage.verify_group(self.root, 'catch')

    def test_missing_shard_fails(self):
        self.fixture()
        (self.root / 'shard-1/discovery.json').unlink()
        with self.assertRaisesRegex(ValueError, 'indexes'):
            coverage.verify_group(self.root, 'catch')

    def test_failed_catch_case_fails(self):
        self.fixture()
        path = self.root / 'shard-1/results.xml'
        path.write_text(path.read_text().replace('success="true"', 'success="false"'))
        with self.assertRaisesRegex(ValueError, 'failed or skipped'):
            coverage.verify_group(self.root, 'catch')

    def test_complete_qt_coverage(self):
        self.fixture('qt')
        self.assertEqual(coverage.verify_group(self.root, 'qt')['cases'], 2)

    def test_catch_skip_attribute_fails(self):
        self.fixture()
        path = self.root / 'shard-1/results.xml'
        path.write_text(path.read_text().replace('success="true"', 'success="true" skips="1"'))
        with self.assertRaisesRegex(ValueError, 'failed or skipped'):
            coverage.verify_group(self.root, 'catch')

    def test_qt_skip_fails(self):
        self.fixture('qt')
        path = self.root / 'shard-1/artifacts/qt-tests.xml'
        path.write_text(path.read_text().replace('<testsuite>', '<testsuite skipped="1">'))
        with self.assertRaisesRegex(ValueError, 'failed or skipped'):
            coverage.verify_group(self.root, 'qt')

    def test_qt_case_skip_without_summary_fails(self):
        self.fixture('qt')
        path = self.root / 'shard-1/artifacts/qt-tests.xml'
        path.write_text(path.read_text().replace('/>', '><skipped/></testcase>'))
        with self.assertRaisesRegex(ValueError, 'failed or skipped'):
            coverage.verify_group(self.root, 'qt')


if __name__ == '__main__':
    unittest.main()
