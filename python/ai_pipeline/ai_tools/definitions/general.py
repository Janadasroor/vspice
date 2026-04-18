# General Purpose & Library tools
GENERAL_TOOLS = [
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
        "name": "assign_real_world_part",
        "description": "Searches for a real-world component (e.g. '10V 2A Schottky diode SMD') via supply chain/lookup and assigns its MPN (Manufacturer Part Number) and value to a generic symbol in the active schematic.",
        "parameters": {
            "type": "object",
            "properties": {
                "reference": {"type": "string", "description": "The reference designator of the generic part to update, e.g., 'D1' or 'U2'."},
                "search_query": {"type": "string", "description": "The natural language query describing the required specifications, e.g., '10V 2A Schottky diode SMD' or 'low noise rail-to-rail op-amp'."}
            },
            "required": ["reference", "search_query"]
        }
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
    {
        "name": "create_netlist_file",
        "description": "Creates a standalone SPICE netlist (.cir) file ready for simulation. Use this when the user asks to design or save a circuit as a netlist.",
        "parameters": {
            "type": "object",
            "properties": {
                "filename": {"type": "string", "description": "e.g. 'boost_converter.cir'"},
                "content": {"type": "string", "description": "The complete SPICE netlist text."}
            },
            "required": ["filename", "content"]
        }
    },
    {
        "name": "remember_fact",
        "description": "Saves a specific fact, user preference, or instruction to the project's permanent memory. Use this when the user says 'remember this' or 'don't do this again'.",
        "parameters": {
            "type": "object",
            "properties": {
                "fact": {"type": "string", "description": "The concise fact or rule to remember (e.g. 'User prefers 1k resistors for pull-ups')"},
                "category": {"type": "string", "enum": ["preference", "rule", "knowledge", "fix"], "default": "knowledge"}
            },
            "required": ["fact"]
        }
    },
    {
        "name": "synthesize_subcircuit",
        "description": "Synthesizes a SPICE .subckt macro definition from natural language and saves it to a .sub file. Call this when the user asks you to create a custom component model, subcircuit, or macro (like a 555 timer or op-amp model).",
        "parameters": {
            "type": "object",
            "properties": {
                "name": {
                    "type": "string",
                    "description": "A short, descriptive name for the subcircuit (e.g. 'NE555' or 'LM7805')."
                },
                "description": {
                    "type": "string",
                    "description": "A brief description of the subcircuit."
                },
                "subcircuit_code": {
                    "type": "string",
                    "description": "The complete, raw SPICE text containing the .subckt definition, including the pins/nodes and internal components, ending with .ends."
                }
            },
            "required": ["name", "description", "subcircuit_code"],
        },
    },
]
