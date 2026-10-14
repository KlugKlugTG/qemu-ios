#!/usr/bin/env python3
"""Small host-only checks for the operator driver; no compiler/device inputs."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location('candidate', Path(__file__).resolve().parents[1] /
                                              'contrib/portable/run-candidate.py')
candidate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(candidate)


class Candidate(unittest.TestCase):
    def test_preflight_mismatch_never_starts_command_or_creates_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            pin = root / 'input'
            pin.write_text('actual')
            output = root / 'new'
            plan = {'schema': 1, 'android_api': 28, 'jobs': 1,
                    'files': [{'path': str(pin), 'sha256': '0' * 64}], 'tools': [],
                    'run_output': str(output), 'targets': [], 'path_environment': os.environ['PATH']}
            with mock.patch.object(candidate, 'source_check'), mock.patch.object(candidate.subprocess, 'Popen') as run:
                with self.assertRaisesRegex(RuntimeError, 'input changed'):
                    candidate.preflight(plan)
                run.assert_not_called()
            self.assertFalse(output.exists())

    def test_actual_bounded_host_command_records_exit_without_compilation(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp)
            plan = {'max_group_rss_kib': 128 * 1024, 'path_environment': os.environ['PATH']}
            rows = []
            with mock.patch.object(candidate, 'source_check'):
                candidate.run_step(plan, output, 'host-only', ['/usr/bin/python3', '-c', 'print("host-only")'],
                                   output, 5, rows)
            self.assertEqual(rows[0]['exit'], 0)
            self.assertIn('host-only', (output / 'host-only.log').read_text())
            self.assertEqual(json.loads((output / 'commands.json').read_text())[0]['pid'], rows[0]['pid'])
            self.assertFalse(Path('/proc', str(rows[0]['pid'])).exists())


if __name__ == '__main__':
    unittest.main()
