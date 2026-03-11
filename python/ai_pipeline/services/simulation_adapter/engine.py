import subprocess
import json
import os
import sys

class SimulationAdapter:
    def __init__(self, flux_cmd_path=None):
        # Resolve path relative to project root with sensible fallbacks.
        candidates = []
        if flux_cmd_path:
            candidates.append(os.path.abspath(flux_cmd_path))
        candidates.extend([
            os.path.abspath("build/dev-debug/flux-cmd"),
            os.path.abspath("build/flux-cmd"),
            "flux-cmd",
        ])
        self.flux_cmd_path = next((p for p in candidates if p == "flux-cmd" or os.path.exists(p)), candidates[0])
        self.last_results = None

    def list_nodes(self, schematic_path):
        """Returns a list of all nodes/signals available in the schematic."""
        results = self.run_simulation(schematic_path, analysis_type="op")
        if not results:
            return []
        nodes = list(results.get("nodeVoltages", {}).keys())
        signals = [w["name"] for w in results.get("waveforms", [])]
        return sorted(list(set(nodes + signals)))

    def run_simulation(self, schematic_path, analysis_type="op", stop_time="10m", step_size="100u"):
        if not os.path.exists(schematic_path):
            raise FileNotFoundError(f"Schematic file not found: {schematic_path}")

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
                print(f"Simulation produced no output. Error: {result.stderr}", file=sys.stderr)
                return None
            
            # Find the JSON part in case there's extra logging.
            output = result.stdout.strip()
            # Try to find the last complete JSON block in case of multiple outputs
            try:
                # Find all '{' and try to parse from each one backwards
                potential_json = []
                for i in range(len(output)):
                    if output[i] == '{':
                        # Find matching '}' or just take to the end and let json.loads decide
                        for j in range(len(output)-1, i, -1):
                            if output[j] == '}':
                                segment = output[i:j+1]
                                try:
                                    parsed = json.loads(segment)
                                    potential_json.append(parsed)
                                    break
                                except:
                                    continue
                
                if potential_json:
                    # Take the most comprehensive one (usually the last one)
                    self.last_results = potential_json[-1]
                    return self.last_results
            except:
                pass
            
            # Simple fallback
            if "{" in output and "}" in output:
                json_start = output.find("{")
                json_end = output.rfind("}")
                json_data = output[json_start:json_end + 1]
                self.last_results = json.loads(json_data)
                return self.last_results
            return None
        except Exception as e:
            print(f"Simulation failed: {str(e)}", file=sys.stderr)
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
