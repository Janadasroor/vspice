import json
import os
import sys
import tempfile
import unittest

sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '../../..')))

from python.ai_pipeline.ai_tools.tools import ToolRegistry


class _FakeAdapter:
    def __init__(self, results):
        self.last_results = None
        self._results = results

    def run_simulation(self, schematic_path, analysis_type="op", stop_time="10m", step_size="100u"):
        self.last_results = self._results
        return self._results

    def list_nodes(self, schematic_path):
        self.run_simulation(schematic_path, analysis_type="op")
        waves = [w.get("name", "") for w in self._results.get("waveforms", [])]
        return waves

    def get_signal(self, signal_name):
        if not self.last_results:
            return None, None
        for wave in self.last_results.get("waveforms", []):
            if str(wave.get("name", "")).upper() == signal_name.upper():
                return wave.get("x"), wave.get("y")
        return None, None


def _write_temp_schematic(items):
    fd, path = tempfile.mkstemp(suffix=".sch", text=True)
    os.close(fd)
    with open(path, "w", encoding="utf-8") as f:
        json.dump({"items": items}, f)
    return path


class TestToolRegistryPowerSource(unittest.TestCase):
    def test_voltage_source_average_power_from_nominal_voltage_and_current_waveform(self):
        sch = _write_temp_schematic([
            {"reference": "V1", "type": "Voltage_Source_DC", "value": "5V"}
        ])
        self.addCleanup(lambda: os.path.exists(sch) and os.remove(sch))

        results = {
            "waveforms": [
                {"name": "V(Net1)", "x": [0.0, 1.0, 2.0], "y": [5.0, 5.0, 5.0]},
                {"name": "I(V1)", "x": [0.0, 1.0, 2.0], "y": [2.0, 2.0, 2.0]},
            ],
            "nodeVoltages": {},
            "branchCurrents": {},
        }
        tools = ToolRegistry(sch, adapter=_FakeAdapter(results))
        out = tools.compute_average_power("V1")
        self.assertNotIn("error", out)
        self.assertAlmostEqual(out["average_power_w"], 10.0, places=6)
        self.assertEqual(out["unit"], "W")

    def test_circuit_target_auto_selects_single_voltage_source(self):
        sch = _write_temp_schematic([
            {"reference": "V1", "type": "Voltage_Source_DC", "value": "12V"}
        ])
        self.addCleanup(lambda: os.path.exists(sch) and os.remove(sch))

        results = {
            "waveforms": [
                {"name": "I(V1)", "x": [0.0, 1.0], "y": [0.5, 0.5]},
            ],
            "nodeVoltages": {},
            "branchCurrents": {},
        }
        tools = ToolRegistry(sch, adapter=_FakeAdapter(results))
        out = tools.compute_average_power("circuit")
        self.assertNotIn("error", out)
        self.assertEqual(out["target"], "V1")
        self.assertAlmostEqual(out["average_power_w"], 6.0, places=6)

    def test_voltage_source_target_requires_disambiguation_when_multiple_sources(self):
        sch = _write_temp_schematic([
            {"reference": "V1", "type": "Voltage_Source_DC", "value": "5V"},
            {"reference": "V2", "type": "Voltage_Source_DC", "value": "3.3V"},
        ])
        self.addCleanup(lambda: os.path.exists(sch) and os.remove(sch))

        tools = ToolRegistry(sch, adapter=_FakeAdapter({"waveforms": [], "nodeVoltages": {}, "branchCurrents": {}}))
        out = tools.compute_average_power("voltage_source")
        self.assertIn("error", out)
        self.assertIn("Multiple voltage sources", out["error"])


if __name__ == "__main__":
    unittest.main()
