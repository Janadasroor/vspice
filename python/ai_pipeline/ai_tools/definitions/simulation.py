# Simulation & Analysis tools
SIMULATION_TOOLS = [
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
        "name": "setup_parameter_sweep",
        "description": "Configures and runs a simulation sweeping a specific parameter (e.g., sweeping R1 from 1k to 10k). This is extremely useful for analyzing how a component's value affects the circuit's performance.",
        "parameters": {
            "type": "object",
            "properties": {
                "component_ref": {"type": "string", "description": "The reference of the component to sweep (e.g., 'R1', 'C2', 'V1')."},
                "sweep_start": {"type": "string", "description": "The starting value of the sweep (e.g., '1k')."},
                "sweep_stop": {"type": "string", "description": "The ending value of the sweep (e.g., '10k')."},
                "sweep_step": {"type": "string", "description": "The step size of the sweep (e.g., '1k')."},
                "analysis_type": {"type": "string", "enum": ["op", "tran", "ac"], "description": "The type of analysis to run for each sweep step."},
                "analysis_args": {"type": "string", "description": "The arguments for the analysis (e.g., '1u 5m' for tran or 'dec 10 1 100k' for ac)."}
            },
            "required": ["component_ref", "sweep_start", "sweep_stop", "sweep_step", "analysis_type", "analysis_args"]
        }
    },
]
