# To run this code you need to install the following dependencies:
# pip install google-genai numpy

import base64
import os
import sys
from google import genai
from google.genai import types
import json
import argparse
import random
import time
import re

# Add ai_pipeline to path
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))
try:
    from ai_pipeline.ai_tools.tools import ToolRegistry, get_tools_schema
    from ai_pipeline.services.project_context.retriever import ProjectContextRetriever
    from ai_pipeline.agents.specialized.simulation_agent import SimulationAgent
except ImportError:
    # Fallback if structure is different
    ToolRegistry = None
    get_tools_schema = None
    ProjectContextRetriever = None
    SimulationAgent = None

def _is_retryable_error(exc):
    message = str(exc).lower()
    retry_tokens = [
        "aborterror", "aborted", "timed out", "timeout", "deadline exceeded",
        "connection reset", "temporarily unavailable", "service unavailable",
        "internal", "429", "503",
    ]
    return any(token in message for token in retry_tokens)

def list_available_models(api_key):
    client = genai.Client(api_key=api_key)
    model_names = []
    seen = set()
    for model in client.models.list():
        raw_name = getattr(model, "name", "") or ""
        if not raw_name:
            continue
        normalized = raw_name.split("/", 1)[1] if raw_name.startswith("models/") else raw_name
        lowered = normalized.lower()
        if "gemini" not in lowered:
            continue
        if normalized not in seen:
            seen.add(normalized)
            model_names.append(normalized)
    model_names.sort()
    return model_names

def _parse_time_to_seconds(token):
    token = token.strip().lower()
    m = re.match(r"^([0-9]*\.?[0-9]+)\s*(s|ms|us|ns)?$", token)
    if not m:
        return None
    value = float(m.group(1))
    unit = m.group(2) or "s"
    scale = {"s": 1.0, "ms": 1e-3, "us": 1e-6, "ns": 1e-9}
    return value * scale.get(unit, 1.0)

def _extract_time_window_seconds(prompt):
    p = prompt.lower()
    m = re.search(r"from\s+([0-9]*\.?[0-9]+\s*(?:s|ms|us|ns)?)\s+to\s+([0-9]*\.?[0-9]+\s*(?:s|ms|us|ns)?)", p)
    if not m:
        return None, None
    return _parse_time_to_seconds(m.group(1)), _parse_time_to_seconds(m.group(2))

def _infer_power_target(prompt):
    p = prompt.lower()
    if "voltage source" in p:
        return "voltage_source", False
    if any(x in p for x in ["entire circuit", "whole circuit", "total circuit", "average power of this circuit"]):
        return "circuit", False

    comp_match = re.search(r"\b([RCLDQMUVX]\d+)\b", prompt, re.IGNORECASE)
    if comp_match:
        return comp_match.group(1).upper(), True

    node_match = re.search(r"\bnode\s+([a-z0-9_]+)\b", p)
    if node_match:
        return f"node:{node_match.group(1)}", True

    vparen = re.search(r"\bV\(([A-Za-z0-9_]+)\)", prompt)
    if vparen:
        return f"node:{vparen.group(1)}", True

    return "circuit", False

def _extract_source_candidates(nodes):
    candidates = []
    seen = set()
    for n in nodes:
        m = re.match(r"^I\(([^)]+)\)$", str(n).strip(), re.IGNORECASE)
        if m:
            src = m.group(1).strip().upper()
            if src not in seen:
                seen.add(src)
                candidates.append(src)
    return candidates

def _maybe_answer_average_power_direct(prompt, registry):
    p = prompt.lower()
    if "average power" not in p and "avg power" not in p:
        return None

    target, explicit_target = _infer_power_target(prompt)
    t_start, t_end = _extract_time_window_seconds(prompt)

    result = registry.compute_average_power(target=target, t_start=t_start, t_end=t_end)
    if result and not result.get("error"):
        return (
            f"Result: {result.get('average_power_w'):.6g} W\n"
            f"Scope: target={result.get('target')}, window={result.get('t_start')}s to {result.get('t_end')}s\n"
            "Assumptions: computed from available simulation signals."
        )

    error_text = (result or {}).get("error", "")
    if target == "circuit" and "limited" in error_text.lower() and not explicit_target:
        nodes_res = registry.list_nodes()
        nodes = nodes_res.get("nodes", []) if isinstance(nodes_res, dict) else []
        source_candidates = _extract_source_candidates(nodes)[:6]
        if source_candidates:
            examples = ", ".join(source_candidates)
            return (
                "I can compute average power automatically once I know the source/target. "
                f"Detected likely source candidates: {examples}. "
                f"Please confirm one target (for example: {source_candidates[0]})."
            )
        return (
            "I can compute average power, but this project currently needs a specific target "
            "(component or node). Please provide one target like R1, V1, or node:VOUT."
        )
    return None

def generate(prompt="", context="", mode="schematic", history="", image_base64="", project_path="", retries=3, model=""):
    api_key = os.environ.get("GEMINI_API_KEY")
    if not api_key:
        print("Error: GEMINI_API_KEY environment variable not set.", file=sys.stderr)
        return

    client = genai.Client(api_key=api_key)
    common_instructions = """
You can use <ACTION>Doing something...</ACTION> tags to update the user on your current step.
For example: <ACTION>Choosing better symbol technic...</ACTION>

You can also provide interactive buttons for the user to trigger actions.
Use <SUGGESTION>Label|command</SUGGESTION> format at the end of your response.
Examples:
- <SUGGESTION>Plot VOUT|plot node:VOUT</SUGGESTION>
- <SUGGESTION>Run DRC|drc</SUGGESTION>
- <SUGGESTION>Auto-Route All|route_all</SUGGESTION>
- <SUGGESTION>Optimize Placement|optimize_placement</SUGGESTION>
- <SUGGESTION>Generate BOM|bom</SUGGESTION>

To help the user find components on the canvas, use <HIGHLIGHT>Ref1,Ref2</HIGHLIGHT> tag.
Example: "The feedback resistor <HIGHLIGHT>R5</HIGHLIGHT> sets the gain."
"""

    # Define the System Instruction
    if mode == "symbol":
        system_context = common_instructions + "You are FluxAI, an expert EDA symbol generator..."
    elif mode == "pcb":
        system_context = common_instructions + """
    You are FluxAI, an expert PCB Layout Engineer.
    You have access to a set of tools to interact with the PCB editor.

    STRATEGY:
    1. Placement: Use execute_pcb_commands to place footprints (addComponent).
    2. Routing: Use execute_pcb_commands to add traces (addTrace) and vias (addVia).
    3. Verification: Use execute_pcb_commands to run DRC (runDRC).
    4. Propose Fixes: If DRC errors are found, propose layout changes using execute_pcb_commands.

    RESPONSE STRUCTURE:
    1) Technical analysis of the layout.
    2) Proposed changes.
    3) Command snippets: Wrap your PCB commands JSON in <SNIPPET>{"commands": [...]}</SNIPPET> tags.
    4) Suggestions (Optional buttons using <SUGGESTION> tag)
    """
    elif mode == "logic":
        system_context = common_instructions + """
You are FluxAI, an expert EDA Python assistant for the Viospice Smart Signal Block layout.
Your goal is to write custom Python algorithms for the `update(self, t, inputs)` function.
If the user asks you to save the generated script for later use, you MUST use the `save_logic_template('filename.py', 'code')` tool to write it directly to their template library.
"""
    elif mode == "ask":
        system_context = common_instructions + """
You are FluxAI, an expert Electronic Design Architect.
Your goal is to explain and analyze circuits for the user.
You have access to the full schematic context.

GUIDELINES:
1. Visual Descriptions: Describe the circuit topology clearly (e.g. "This looks like a buck converter with L1 and D1...").
2. Functional Analysis: Explain how parts of the circuit work together.
3. Design Audit: If the user asks for an audit or review:
   - Perform a "sanity check" on component values (e.g. "100uF for a decoupling cap seems excessive here").
   - Check for missing essential components (decoupling capacitors, pull-up resistors, protection diodes).
   - Analyze power dissipation if simulation data is available (via compute_average_power).
   - Identify potential reliability risks (e.g. "VGS of Q1 is near its maximum rating").
4. Professionalism: Be technical, clear, and encouraging. Use <HIGHLIGHT> tags for components you mention.
"""
    else:
        system_context = common_instructions + """
    You are FluxAI, an expert Electronic Design Automation (EDA) co-pilot.
    You have access to a set of tools to interact with a circuit simulator and research electronic components.

    STRATEGY (MANDATORY):
    1. Discovery: If you are unsure about component names or nodes, ALWAYS call list_nodes() first.
    2. Direct Action: If the user asks for power/voltage/current, and you know the target name (e.g. V1, R1, Net1), call the tool immediately.
    3. Visuals: If the user asks to "see", "show", "plot", or "visualize" a signal, ALWAYS use the plot_signal tool.
    4. Design Repair: If you see 'erc_violations' in the context, or the user asks to fix an error, use execute_commands to propose a fix (e.g. adding a pull-up, grounding a pin, or connecting two nodes).
    5. Multi-Step Design: If the user asks to "add a circuit", use execute_commands to place all components and connect them properly. Use absolute coordinates if possible, or relative to a known component.
    6. Research & Sourcing: If the user asks for a part recommendation or technical specs (e.g. 'Find an Op-Amp', 'LM317 pinout'), use the web_search or lookup_component_data tools. 
    - Propose specific part numbers based on the research.
    - If you find a matching part for a generic component in the schematic, offer to update it using a <SNIPPET> or execute_commands with the correct value and properties. The user will see an 'APPLY COMPONENT SPECS' button.
    7. Symbol Library: If you generate a SPICE netlist, you have access to the exact schematic components listed in the `available_symbols` array inside your Context payload. Only use these exact part names in your SPICE netlists or instructions. Do not guess or hallucinate component names like 'S' when 'CSW' is available, for example.
    8. Professionalism: Be precise and concise. Avoid technical filler like "Context attached".

    RESPONSE STRUCTURE:
    1) Result (value + unit)
    2) Scope (target + time window analyzed)
    3) Assumptions (e.g. "Used DC Operating Point")
    4) Command snippets: If you use execute_commands, wrap the resulting JSON in <SNIPPET>{"commands": [...]}</SNIPPET> tags so the user can apply it.
    5) Suggestions (Optional buttons using <SUGGESTION> tag)
    """
    extra_context = ""
    if project_path and ProjectContextRetriever:
        retriever = ProjectContextRetriever(project_path)
        pc = retriever.get_project_context()
        if pc:
            extra_context = f"\n\nProject-Wide Multi-Sheet Context (other sheets):\n{pc}"

    full_prompt = prompt
    if context:
        full_prompt = f"{system_context}\n\nContext:\n{context}{extra_context}\n\nUser Request: {prompt}"
    else:
        full_prompt = f"{system_context}{extra_context}\n\nUser Request: {prompt}"

    model_name = model or "gemini-2.0-flash-thinking-exp-01-21"
    
    contents = []
    if history:
        try:
            history_data = json.loads(history)
            # Smart Memory Management: Keep only the most recent contextually relevant turns (e.g. last 10 messages)
            # to prevent token overflow and model distraction. Ensure we keep alternating user/model if possible.
            MAX_HISTORY_MESSAGES = 10
            if len(history_data) > MAX_HISTORY_MESSAGES:
                history_data = history_data[-MAX_HISTORY_MESSAGES:]
            
            for msg in history_data:
                role = msg.get("role", "user")
                if role not in ["user", "model"]: role = "user" 
                
                # Truncate extremely long past responses to save context
                text = msg.get("text", "")
                if role == "model" and len(text) > 2000:
                    text = text[:2000] + "... [Content Truncated for Memory Optimization]"
                    
                contents.append(types.Content(role=role, parts=[types.Part.from_text(text=text)]))
        except: pass

    user_parts = [types.Part.from_text(text=full_prompt)]
    if image_base64:
        try:
            user_parts.append(types.Part.from_bytes(data=base64.b64decode(image_base64), mime_type="image/png"))
        except: pass
    contents.append(types.Content(role="user", parts=user_parts))

    # Prepare tools
    tools_config = []
    registry = None
    if project_path and ToolRegistry and get_tools_schema:
        registry = ToolRegistry(project_path)
        tools_config = [types.Tool(function_declarations=get_tools_schema())]

    # Tool calling loop (Manual Mode / Fallback)
    for _ in range(5):
        try:
            response = client.models.generate_content(
                model=model_name,
                contents=contents,
                config=types.GenerateContentConfig(
                    thinking_config=types.ThinkingConfig(include_thoughts=True),
                    tools=tools_config
                )
            )
            
            if response.candidates[0].content.parts:
                thought = response.candidates[0].content.parts[0].thought if hasattr(response.candidates[0].content.parts[0], 'thought') else None
                if thought:
                    print(f"<THOUGHT>{thought}</THOUGHT>", end="", flush=True)

            tool_calls = [p.function_call for p in response.candidates[0].content.parts if p.function_call]
            
            if not tool_calls:
                # Final response
                for part in response.candidates[0].content.parts:
                    if part.text:
                        cleaned = re.sub(r"(?i)(?:^|\s)[\u25c8*•\-]*\s*context attached\b", " ", part.text)
                        cleaned = re.sub(r"\n{3,}", "\n\n", cleaned).strip()
                        print(cleaned, end="", flush=True)
                return

            contents.append(response.candidates[0].content)
            tool_results_parts = []
            for call in tool_calls:
                print(f"<ACTION>Running tool: {call.name}...</ACTION>", end="", flush=True)
                func = getattr(registry, call.name, None) if registry else None
                result = func(**call.args) if func else {"error": "Tool not available"}
                tool_results_parts.append(types.Part.from_function_response(name=call.name, response=result))
            
            contents.append(types.Content(role="user", parts=tool_results_parts))
            
        except Exception as e:
            print(f"\nError during generation: {e}", file=sys.stderr)
            return

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("prompt", nargs="?", default="Hello!")
    parser.add_argument("--context", help="Context in JSON format")
    parser.add_argument("--history", help="Conversation history in JSON format", default="")
    parser.add_argument("--image", help="Base64 encoded PNG image", default="")
    parser.add_argument("--project_path", help="Current schematic/project file path", default="")
    parser.add_argument("--mode", default="schematic")
    parser.add_argument("--retries", type=int, default=3)
    parser.add_argument("--model", default="")
    parser.add_argument("--list-models", action="store_true")
    args = parser.parse_args()

    if args.list_models:
        api_key = os.environ.get("GEMINI_API_KEY")
        if not api_key:
            print("Error: GEMINI_API_KEY environment variable not set.", file=sys.stderr)
            sys.exit(1)
        try:
            models = list_available_models(api_key)
            print(json.dumps(models))
            sys.exit(0)
        except Exception as e:
            print(f"Error fetching models: {e}", file=sys.stderr)
            sys.exit(1)

    generate(args.prompt, args.context, args.mode, args.history, args.image, args.project_path, args.retries, args.model)
