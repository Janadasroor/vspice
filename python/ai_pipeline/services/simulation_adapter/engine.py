import subprocess
import json
import os
import sys
import shutil

class SimulationAdapter:
    def __init__(self, flux_cmd_path=None):
        repo_root = os.path.abspath(
            os.path.join(os.path.dirname(__file__), "..", "..", "..", "..")
        )
        def repo_path(*parts):
            return os.path.join(repo_root, *parts)

        # Resolve CLI path relative to project root with sensible fallbacks.
        candidates = []
        if flux_cmd_path:
            candidates.append(os.path.abspath(flux_cmd_path))
        candidates.extend([
            repo_path("build", "vio-cmd"),
            repo_path("build-debug", "vio-cmd"),
            repo_path("build-asan", "vio-cmd"),
            repo_path("build", "dev-debug", "vio-cmd"),
            repo_path("build", "flux-cmd"),
            repo_path("build-debug", "flux-cmd"),
            repo_path("build-asan", "flux-cmd"),
            repo_path("build", "dev-debug", "flux-cmd"),
            "vio-cmd",
            "flux-cmd",
        ])
        self.flux_cmd_path = self._resolve_cli_path(candidates)
        self.last_results = None
        self.last_error = None

    @staticmethod
    def _resolve_cli_path(candidates):
        for p in candidates:
            if os.path.isabs(p) and os.path.isfile(p) and os.access(p, os.X_OK):
                return p
            if not os.path.isabs(p):
                resolved = shutil.which(p)
                if resolved:
                    return resolved
        return candidates[0] if candidates else "vio-cmd"

    def list_nodes(self, schematic_path):
        """Returns a list of all nodes/signals available in the schematic."""
        results = self.run_simulation(schematic_path, analysis_type="op")
        if not results:
            return []
        nodes = list(results.get("nodeVoltages", {}).keys())
        signals = [w["name"] for w in results.get("waveforms", [])]
        return sorted(list(set(nodes + signals)))

    def run_simulation(self, schematic_path, analysis_type="op", stop_time="10m", step_size="100u"):
        self.last_error = None
        if not os.path.exists(schematic_path):
            self.last_error = f"Schematic file not found: {schematic_path}"
            return None

        if not (
            (os.path.isabs(self.flux_cmd_path) and os.path.isfile(self.flux_cmd_path) and os.access(self.flux_cmd_path, os.X_OK))
            or shutil.which(self.flux_cmd_path)
        ):
            self.last_error = (
                f"Simulation CLI not found: '{self.flux_cmd_path}'. Build `vio-cmd` "
                "or add it to PATH."
            )
            return None

        cmd = [
            self.flux_cmd_path,
            "simulate",
            schematic_path,
            "--analysis", analysis_type,
            "--json"
        ]
        if analysis_type == "tran":
            cmd.extend(["--stop", stop_time, "--step", step_size])

        try:
            # We use _Exit(0) in flux-cmd, so it might return 0 but we should check stdout.
            result = subprocess.run(cmd, capture_output=True, text=True)
            # Some builds may not expose --json; retry once without it.
            if "Unknown option 'json'" in (result.stderr or ""):
                cmd = [c for c in cmd if c != "--json"]
                result = subprocess.run(cmd, capture_output=True, text=True)
            if not result.stdout.strip():
                self.last_error = f"Simulation produced no output. Error: {(result.stderr or '').strip()}"
                return None
            
            # Find the JSON part in case there's extra logging.
            output = result.stdout.strip()
            if not output:
                self.last_error = f"Simulation produced no output. Error: {(result.stderr or '').strip()}"
                return None

            # Fast search for the JSON block
            json_start = output.find('{')
            json_end = output.rfind('}')
            
            if json_start != -1 and json_end != -1 and json_end > json_start:
                try:
                    json_data = output[json_start : json_end + 1]
                    self.last_results = json.loads(json_data)
                    return self.last_results
                except json.JSONDecodeError as e:
                    self.last_error = f"Failed to parse JSON: {str(e)}"
            
            self.last_error = "Simulation output did not contain valid JSON."
            return None
        except Exception as e:
            self.last_error = f"Simulation failed: {str(e)}"
            return None

    def get_signal(self, signal_name):
        if not self.last_results:
            return None, None
        
        target = str(signal_name).strip().upper()
        
        # 1. Try exact or case-insensitive match in waveforms
        for wave in self.last_results.get("waveforms", []):
            name = str(wave.get("name", "")).strip().upper()
            if name == target:
                return wave.get("x"), wave.get("y")
        
        # 2. Try adding/removing V() or I() wrappers
        alt_targets = []
        if target.startswith("V(") and target.endswith(")"):
            alt_targets.append(target[2:-1])
        else:
            alt_targets.append(f"V({target})")
            
        if target.startswith("I(") and target.endswith(")"):
            alt_targets.append(target[2:-1])
        else:
            alt_targets.append(f"I({target})")

        for alt in alt_targets:
            for wave in self.last_results.get("waveforms", []):
                if str(wave.get("name", "")).strip().upper() == alt:
                    return wave.get("x"), wave.get("y")

        # 3. Match in nodeVoltages
        clean_name = target
        if clean_name.startswith("V(") and clean_name.endswith(")"):
            clean_name = clean_name[2:-1]
            
        node_voltages = self.last_results.get("nodeVoltages", {})
        # Check case-insensitive
        for node, val in node_voltages.items():
            if str(node).upper() == clean_name:
                return [0.0], [val]

        # 4. Branch current fallback
        branch = self.last_results.get("branchCurrents", {})
        clean_i = target
        if clean_i.startswith("I(") and clean_i.endswith(")"):
            clean_i = clean_i[2:-1]
            
        for b, val in branch.items():
            ub = str(b).upper()
            if ub == clean_i or ub == target:
                return [0.0], [val]
            
        return None, None
