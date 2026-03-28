import json
import re
import os
from ..services.simulation_adapter.engine import SimulationAdapter
from ..services.power_metrics.compute import compute_average_power
from ..services.waveform_viz.plot_service import PlotService
from ..services.supply_chain.service import SupplyChainService


class ToolRegistry:
    def __init__(self, schematic_path, adapter=None, octopart_api_key=None):
        self.schematic_path = schematic_path
        self.adapter = adapter or SimulationAdapter()
        self.supply_chain = SupplyChainService(api_key=octopart_api_key)

    def _load_schematic_items(self):
        with open(self.schematic_path, "r", encoding="utf-8") as f:
            doc = json.load(f)
        return doc.get("items", []) if isinstance(doc, dict) else []

    def plot_signal(self, signal_name):
        """
        Generates a visual plot of a signal (voltage or current).
        Returns a base64 encoded PNG image string wrapped in a tool result.
        """
        try:
            self._ensure_tran_results()
            x, y = self.adapter.get_signal(signal_name)
            
            if x is None or y is None:
                # Try discovery
                nodes_res = self.list_nodes()
                nodes = nodes_res.get("nodes", [])
                return {"error": f"Signal '{signal_name}' not found. Available signals include: {', '.join(nodes[:10])}..."}

            img_b64 = PlotService.plot_waveform(x, y, signal_name)
            return {
                "signal": signal_name,
                "plot_png_base64": img_b64,
                "info": f"Generated plot for {signal_name} with {len(x)} data points."
            }
        except Exception as e:
            return {"error": str(e)}

    def generate_snippet(self, description, items_json):
        """
        Generates a sub-circuit snippet based on a design description.
        items_json: A string containing a JSON array of schematic items.
        Returns the snippet wrapped in <SNIPPET> tags for UI placement.
        """
        # We just return it; the query script will ensure it's tagged for the UI
        return {
            "description": description,
            "snippet": items_json,
            "instructions": "Click the 'PLACE AI SNIPPET' button in the chat to add this to your schematic."
        }

    def generate_schematic_from_netlist(self, netlist_text):
        """
        Generates a full schematic by providing a pure SPICE netlist string.
        Returns the netlist wrapped so the UI can draw the 'GENERATE SCHEMATIC' action button.
        """
        return {
            "status": "netlist_ready",
            "netlist": netlist_text,
            "instructions": "A 'GENERATE SCHEMATIC' button will appear in the chat. Click it to render this netlist directly onto the canvas."
        }

    def save_logic_template(self, filename, code):
        """
        Saves a custom generated Python logic script directly to the python/templates directory
        so the user can load it later.
        """
        try:
            # We are assuming the CWD is the project root (Viospice/build or Viospice/)
            # We'll resolve 'python/templates' relative to the schematic path or current dir.
            # Usually the application root is near the parent of the schematic path, but we can search for it
            # or simply use a hardcoded relative path.
            # Viospice template path is typically 'python/templates/' relative to the binary (which is in bin/).
            # The safest approach is to create it if it doesn't exist in the current working directory, 
            # or next to the query script.
            
            # Use os.getcwd() which is usually the project dir or build dir.
            target_dir = os.path.join(os.getcwd(), "python", "templates")
            
            if not os.path.exists(target_dir):
                # Fallback to relative to this script
                target_dir = os.path.join(os.path.dirname(__file__), "..", "..", "templates")
                if not os.path.exists(target_dir):
                    os.makedirs(target_dir, exist_ok=True)
            
            # Ensure safe filename
            safe_filename = "".join([c for c in filename if c.isalnum() or c in "._-"])
            if not safe_filename.endswith(".py"):
                safe_filename += ".py"
                
            out_path = os.path.join(target_dir, safe_filename)
            with open(out_path, "w", encoding="utf-8") as f:
                f.write(code)
                
            return {
                "status": "template_saved",
                "filename": safe_filename,
                "instructions": f"Saved template '{safe_filename}' successfully to {out_path}."
            }
        except Exception as e:
            return {"error": f"Failed to save template: {str(e)}"}

    def _find_voltage_sources(self):
        sources = []
        try:
            for item in self._load_schematic_items():
                ref = str(item.get("reference", "")).strip()
                typ = str(item.get("type", "")).strip()
                val = str(item.get("value", "")).strip()
                if ref.upper().startswith("V") or "VOLTAGE_SOURCE" in typ.upper():
                    sources.append({"reference": ref, "type": typ, "value": val})
        except Exception:
            pass
        return sources

    @staticmethod
    def _parse_voltage_value(voltage_text):
        s = str(voltage_text).strip().lower()
        m = re.match(r"^\s*([-+]?[0-9]*\.?[0-9]+)\s*([munpk]?)(v)?\s*$", s)
        if not m:
            return None
        value = float(m.group(1))
        prefix = m.group(2)
        scale = {"": 1.0, "m": 1e-3, "u": 1e-6, "n": 1e-9, "p": 1e-12, "k": 1e3}
        return value * scale.get(prefix, 1.0)

    def _ensure_tran_results(self, stop_time="10m", step_size="100u"):
        results = self.adapter.run_simulation(
            self.schematic_path,
            analysis_type="tran",
            stop_time=stop_time,
            step_size=step_size,
        )
        if not results:
            err = getattr(self.adapter, "last_error", None) or "Simulation failed to run."
            raise RuntimeError(err)
        return results

    @staticmethod
    def _timeseries_from_wave(results, signal_name):
        for wave in results.get("waveforms", []):
            if str(wave.get("name", "")).upper() == signal_name.upper():
                x = wave.get("x")
                y = wave.get("y")
                if x is not None and y is not None and len(x) == len(y) and len(x) > 0:
                    return x, y
        return None, None

    @staticmethod
    def _best_time_axis(results):
        for wave in results.get("waveforms", []):
            x = wave.get("x")
            if x and len(x) > 0:
                return x
        return [0.0]

    @staticmethod
    def _resolve_time_window(time_axis, t_start, t_end):
        start = t_start if t_start is not None else (time_axis[0] if time_axis else 0.0)
        end = t_end if t_end is not None else (time_axis[-1] if time_axis else start)
        return start, end

    @staticmethod
    def _avg_from_arrays(time_axis, voltage, current, t_start, t_end):
        avg_p = compute_average_power(time_axis, voltage, current, t_start, t_end)
        start, end = ToolRegistry._resolve_time_window(time_axis, t_start, t_end)
        return avg_p, start, end

    def _compute_voltage_source_power(self, source_ref, t_start=None, t_end=None):
        results = self._ensure_tran_results()
        ref = source_ref.upper()

        # Prefer explicit current waveform I(Vx).
        i_x, i_y = self._timeseries_from_wave(results, f"I({ref})")
        if i_x is None or i_y is None:
            branch = results.get("branchCurrents", {})
            scalar_i = branch.get(ref)
            if scalar_i is None:
                scalar_i = branch.get(f"I({ref})")
            if scalar_i is not None:
                i_x = self._best_time_axis(results)
                i_y = [float(scalar_i)] * len(i_x)

        if i_x is None or i_y is None:
            return {"error": f"Current signal for voltage source '{source_ref}' is not available (expected I({source_ref}))."}

        # Voltage across source: use nominal component value as default when explicit V(source) is unavailable.
        sources = self._find_voltage_sources()
        nominal_v = None
        for src in sources:
            if src["reference"].upper() == ref:
                nominal_v = self._parse_voltage_value(src.get("value", ""))
                break
        if nominal_v is None:
            nominal_v = 0.0

        v_x, v_y = self._timeseries_from_wave(results, f"V({ref})")
        assumptions = []
        if v_x is None or v_y is None:
            v_x = i_x
            v_y = [nominal_v] * len(i_x)
            assumptions.append("Used nominal source voltage from schematic value because V(source) waveform is unavailable.")
        else:
            assumptions.append("Used simulated source voltage waveform and source current waveform.")

        if len(v_x) != len(i_x):
            # Align by using current axis and average voltage if needed.
            mean_v = sum(v_y) / len(v_y) if v_y else nominal_v
            v_x = i_x
            v_y = [mean_v] * len(i_x)
            assumptions.append("Aligned waveform lengths by using average source voltage.")

        avg_p, start, end = self._avg_from_arrays(i_x, v_y, i_y, t_start, t_end)
        return {
            "target": source_ref,
            "average_power_w": avg_p,
            "t_start": start,
            "t_end": end,
            "unit": "W",
            "assumptions": assumptions,
        }

    def list_nodes(self):
        """
        Lists all available nodes and signals in the current circuit.
        Use this to discover which nodes can be queried for voltage or current.
        """
        try:
            nodes = self.adapter.list_nodes(self.schematic_path)
            return {"nodes": nodes, "count": len(nodes)}
        except Exception as e:
            return {"error": f"Failed to list nodes: {str(e)}"}

    def get_signal_data(self, signal_name, analysis_type="tran", stop_time="10m", step_size="100u"):
        """
        Retrieves raw simulation data for a specific signal (node voltage or branch current).
        If the signal name is a node like 'VOUT', it will return 'V(VOUT)'.
        """
        try:
            # Ensure we have a simulation run.
            results = self.adapter.run_simulation(
                self.schematic_path,
                analysis_type=analysis_type,
                stop_time=stop_time,
                step_size=step_size,
            )
            if not results:
                err = getattr(self.adapter, "last_error", None) or "Simulation failed to run."
                return {"error": err}

            x, y = self.adapter.get_signal(signal_name)
            if x is None:
                # Try with V() wrapper.
                if not (signal_name.startswith("V(") or signal_name.startswith("I(")):
                    x, y = self.adapter.get_signal(f"V({signal_name})")

            if x is None:
                return {"error": f"Signal '{signal_name}' not found in results."}

            return {
                "signal": signal_name,
                "points": len(x),
                "t_start": x[0],
                "t_end": x[-1],
                "min": min(y),
                "max": max(y),
                "avg": sum(y) / len(y),
            }
        except Exception as e:
            return {"error": str(e)}

    def compute_average_power(self, target, t_start=None, t_end=None):
        """
        Computes average power for:
        - 'circuit' (auto-selects single voltage source if present)
        - 'voltage_source' (auto-selects single voltage source)
        - explicit source/component like 'V1'
        - node target like 'node:VOUT' (requires V(node) and I(node))
        """
        try:
            normalized = str(target).strip()
            upper_target = normalized.upper()

            if upper_target in {"CIRCUIT", "VOLTAGE_SOURCE"}:
                sources = self._find_voltage_sources()
                if len(sources) == 1:
                    normalized = sources[0]["reference"]
                    upper_target = normalized.upper()
                elif len(sources) == 0:
                    return {"error": "No voltage source was detected in the schematic."}
                else:
                    refs = [s["reference"] for s in sources if s.get("reference")]
                    return {"error": f"Multiple voltage sources detected ({', '.join(refs)}). Please specify one source."}

            # Explicit voltage source target.
            if upper_target.startswith("V") and " " not in upper_target and ":" not in upper_target:
                source_power = self._compute_voltage_source_power(normalized, t_start=t_start, t_end=t_end)
                if "error" not in source_power:
                    return source_power

            # Node power fallback.
            self._ensure_tran_results()
            node_name = normalized.replace("node:", "")
            v_x, v_y = self.adapter.get_signal(f"V({node_name})")
            i_x, i_y = self.adapter.get_signal(f"I({node_name})")

            if v_x is None or i_x is None:
                return {"error": f"Cannot find required signals for target '{target}'. Provide node/component with available V() and I() traces."}

            avg_p = compute_average_power(v_x, v_y, i_y, t_start, t_end)
            start, end = self._resolve_time_window(v_x, t_start, t_end)
            return {
                "target": target,
                "average_power_w": avg_p,
                "t_start": start,
                "t_end": end,
                "unit": "W",
                "assumptions": ["Computed as average(V * I) over the selected time window."],
            }
        except Exception as e:
            return {"error": str(e)}

    def run_simulation(self, analysis_type="tran", stop_time="10m", step_size="100u"):
        """Triggers a simulation run with specific parameters."""
        try:
            res = self.adapter.run_simulation(self.schematic_path, analysis_type, stop_time, step_size)
            if res:
                waveforms = res.get("waveforms", [])
                points = len(waveforms[0].get("x", [])) if waveforms else 0
                return {"status": "success", "analysis": analysis_type, "points": points}
            err = getattr(self.adapter, "last_error", None)
            return {"status": "failed", "error": err} if err else {"status": "failed"}
        except Exception as e:
            return {"error": str(e)}

    def execute_commands(self, commands_json):
        """
        Executes a batch of schematic editor commands (addComponent, addWire, connect, etc.).
        commands_json: A JSON array string of command objects.
        Returns a confirmation message.
        """
        return {
            "status": "commands_ready",
            "commands": commands_json,
            "instructions": "The commands will be executed on the schematic canvas."
        }

    def execute_pcb_commands(self, commands_json):
        """
        Executes a batch of PCB editor commands (addComponent, addTrace, addVia, etc.).
        commands_json: A JSON array string of command objects.
        Returns a confirmation message.
        """
        return {
            "status": "pcb_commands_ready",
            "commands": commands_json,
            "instructions": "The PCB commands will be executed on the layout canvas."
        }

    def web_search(self, query):
        """
        Performs a search for electronic components, datasheets, or technical specs.
        Returns a list of matching components with real-time price and stock if available.
        """
        results = self.supply_chain.search_component(query)
        return {
            "query": query,
            "results": results,
            "status": "search_success" if results else "no_results"
        }

    def lookup_component_data(self, part_number):
        """
        Retrieves detailed technical data for a specific part number, including pinout and key parameters.
        Returns a structured JSON object with component specifications.
        """
        results = self.supply_chain.search_component(part_number)
        if results:
            return {
                "part_number": part_number,
                "data": results[0],
                "status": "lookup_success"
            }
        
        return {
            "part_number": part_number,
            "status": "lookup_simulated",
            "message": f"Technical data for {part_number} requested. Please use your internal knowledge."
        }


def get_tools_schema():
    """Returns the JSON schema definitions for Gemini tool-calling."""
    return [
        {
            "name": "list_nodes",
            "description": "Lists all available nodes and signals in the current circuit.",
            "parameters": {"type": "object", "properties": {}},
        },
        {
            "name": "get_signal_data",
            "description": "Retrieves simulation data summary for a specific signal (node voltage or branch current).",
            "parameters": {
                "type": "object",
                "properties": {
                    "signal_name": {"type": "string", "description": "Name of the signal, e.g., 'VOUT' or 'V(N1)'"},
                    "analysis_type": {"type": "string", "enum": ["op", "tran", "ac"], "default": "tran"},
                    "stop_time": {"type": "string", "description": "Stop time for transient, e.g., '10m'", "default": "10m"},
                    "step_size": {"type": "string", "description": "Step size, e.g., '100u'", "default": "100u"},
                },
                "required": ["signal_name"],
            },
        },
        {
            "name": "compute_average_power",
            "description": "Computes average power for a target: circuit, voltage source, component, or node.",
            "parameters": {
                "type": "object",
                "properties": {
                    "target": {"type": "string", "description": "Target name, e.g., 'circuit', 'voltage_source', 'V1', or 'node:VOUT'"},
                    "t_start": {"type": "number", "description": "Start time in seconds"},
                    "t_end": {"type": "number", "description": "End time in seconds"},
                },
                "required": ["target"],
            },
        },
        {
            "name": "plot_signal",
            "description": "Generates a visual PNG plot of a voltage or current waveform. Use this when the user wants to 'see' or 'visualize' a signal.",
            "parameters": {
                "type": "object",
                "properties": {
                    "signal_name": {
                        "type": "string",
                        "description": "Name of the signal to plot (e.g. 'V(VOUT)', 'I(V1)', 'V1').",
                    },
                },
                "required": ["signal_name"],
            },
        },
        {
            "name": "generate_snippet",
            "description": "Generates a schematic sub-circuit (snippet) based on a functional request (e.g. 'Add a 5V regulator') or a repair request (e.g. 'Fix the floating pin on U1').",
            "parameters": {
                "type": "object",
                "properties": {
                    "description": {
                        "type": "string",
                        "description": "Brief summary of what the snippet does (e.g. 'LM7805 5V Regulator Circuit').",
                    },
                    "items_json": {
                        "type": "string",
                        "description": "JSON string containing an array of schematic items (Resistor, IC, Wire, etc.). Each item MUST have: 'type', 'x', 'y', 'reference', 'value'. Origin (0,0) should be the center of the snippet.",
                    },
                },
                "required": ["description", "items_json"],
            },
        },
        {
            "name": "run_simulation",
            "description": "Triggers a simulation run with specific parameters.",
            "parameters": {
                "type": "object",
                "properties": {
                    "analysis_type": {"type": "string", "enum": ["op", "tran", "ac"]},
                    "stop_time": {"type": "string"},
                    "step_size": {"type": "string"},
                },
                "required": ["analysis_type"],
            },
        },
        {
            "name": "web_search",
            "description": "Performs a search for electronic components, datasheets, or technical specs. Use this when the user asks for part recommendations or generic data (e.g. 'Find a 5V LDO').",
            "parameters": {
                "type": "object",
                "properties": {
                    "query": {"type": "string", "description": "The search query, e.g., 'low-noise rail-to-rail op-amp'"},
                },
                "required": ["query"],
            },
        },
        {
            "name": "lookup_component_data",
            "description": "Retrieves detailed technical data for a specific part number (pinout, operating voltage, key specs). Use this when the user provides a specific part number like 'LM317' or 'NE555'.",
            "parameters": {
                "type": "object",
                "properties": {
                    "part_number": {"type": "string", "description": "The specific part number to lookup, e.g., 'LM7805'"},
                },
                "required": ["part_number"],
            },
        },
        {
            "name": "execute_commands",
            "description": "Executes a batch of high-level schematic manipulation commands (addComponent, addWire, connect, removeComponent, setProperty, runERC, annotate). Use this to add multiple components and connect them properly.",
            "parameters": {
                "type": "object",
                "properties": {
                    "commands_json": {
                        "type": "string",
                        "description": "JSON string containing an array of command objects. Example: [{'cmd': 'addComponent', 'type': 'Resistor', 'x': 0, 'y': 0, 'properties': {'value': '1k'}}, {'cmd': 'connect', 'ref1': 'R1', 'pin1': '1', 'ref2': 'U1', 'pin2': 'VCC'}]",
                    },
                },
                "required": ["commands_json"],
            },
        },
        {
            "name": "execute_pcb_commands",
            "description": "Executes a batch of high-level PCB manipulation commands (addComponent, addTrace, addVia, removeComponent, setProperty, runDRC). Use this to place footprints and route traces.",
            "parameters": {
                "type": "object",
                "properties": {
                    "commands_json": {
                        "type": "string",
                        "description": "JSON string containing an array of PCB command objects. Example: [{'cmd': 'addComponent', 'footprint': 'R_0805', 'x': 10, 'y': 10, 'reference': 'R1'}, {'cmd': 'addTrace', 'points': [{'x': 10, 'y': 10}, {'x': 20, 'y': 10}], 'width': 0.2, 'layer': 0}]",
                    },
                },
                "required": ["commands_json"],
            },
        },
        {
            "name": "generate_schematic_from_netlist",
            "description": "Triggers the automatic generation of a complete electronic schematic by providing a standard SPICE netlist. Use this when the user asks you to design or generate a complete circuit (e.g., 'Make a boost converter circuit'). Ensure the netlist is valid SPICE.",
            "parameters": {
                "type": "object",
                "properties": {
                    "netlist_text": {
                        "type": "string",
                        "description": "The raw SPICE netlist text string. Must contain standard SPICE primitives (R, L, C, Q, M, D, V, I, etc.).",
                    },
                },
                "required": ["netlist_text"],
            },
        },
        {
            "name": "save_logic_template",
            "description": "Saves a custom generated Python logic script directly to the user's template library so they can load it smoothly into Smart Signal Blocks.",
            "parameters": {
                "type": "object",
                "properties": {
                    "filename": {
                        "type": "string",
                        "description": "The filename to save as, e.g., '1khz_pwm.py' or 'fm_modulator.py'."
                    },
                    "code": {
                        "type": "string",
                        "description": "The complete Python source code for the logic block."
                    }
                },
                "required": ["filename", "code"],
            },
        },
    ]
