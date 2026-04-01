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

    return None


def classify_intent(api_key, prompt, model_name="gemini-2.0-flash-exp"):
    """
    Fast, pre-flight classification of user intent to route to the correct subagent.
    """
    client = genai.Client(api_key=api_key)
    routing_prompt = f"""
    Classify the following EDA user prompt into exactly one category: SIMULATION, LAYOUT, or GENERAL.
    
    - SIMULATION: Prompt involves transient/DC analysis, power calculation, plotting waveforms, or debugging simulation errors.
    - LAYOUT: Prompt involves placing components, routing wires, Cartesian coordinates, or PCB footprint placement.
    - GENERAL: General questions about electronics, logic templates, or administrative tasks.
    
    Response must be a single word.
    
    User Prompt: "{prompt}"
    """
    try:
        response = client.models.generate_content(
            model=model_name,
            contents=routing_prompt,
            config=types.GenerateContentConfig(
                temperature=0.0,
                max_output_tokens=10
            )
        )
        intent = response.text.strip().upper()
        if "SIMULATION" in intent: return "SIMULATION"
        if "LAYOUT" in intent: return "LAYOUT"
    except:
        pass
    return "GENERAL"


def generate(prompt="", context="", mode="schematic", history="", instructions="", image_base64="", project_path="", retries=3, model=""):
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

    if instructions:
        common_instructions = f"USER CUSTOM INSTRUCTIONS:\n{instructions}\n\n" + common_instructions

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
"""
    elif mode == "subcircuit":
        system_context = """
You are FluxAI, an expert SPICE Modeling Engineer.
Your goal is to generate a high-quality SPICE .subckt model and a corresponding symbol pin mapping for a requested component.

OUTPUT FORMAT:
You MUST return ONLY a JSON object with the following structure:
{
  "name": "Component Name",
  "subcircuit": "Full SPICE .subckt code here...",
  "mapping": [
    {"num": 1, "label": "PIN_NAME"},
    {"num": 2, "label": "PIN_NAME"}
  ]
}

- The 'subcircuit' MUST be valid SPICE syntax, starting with .subckt and ending with .ends.
- The 'mapping' MUST match the pin order in the .subckt line.
- The 'label' should be a short, human-readable name for the pin (e.g., 'VCC', 'GND', 'IN+').
- Return ONLY the raw JSON object. No markdown, no commentary.
"""
    elif mode == "schematic":
        intent = classify_intent(api_key, prompt, model_name=model or "gemini-2.0-flash-exp")
        print(f"<ACTION>Orchestrator: Routing to {intent} Subagent...</ACTION>", end="", flush=True)
        
        if intent == "SIMULATION":
            system_context = common_instructions + """
You are the Viora Simulation Subagent. You are an expert in SPICE analysis.
Your primary goal is to analyze netlists, find signals (list_nodes), and coordinate simulations (run_simulation).
- Always check node names before plotting.
- Explain simulation failures by analyzing the log output.
- Use compute_average_power for energy analysis.
"""
        elif intent == "LAYOUT":
            system_context = common_instructions + """
You are the Viora Layout Subagent. You specialize in Cartesian coordinate systems and component placement.
Your goal is to manipulate the schematic/PCB canvas (execute_commands, execute_pcb_commands).
- Ensure components are placed on a logic grid.
- Avoid overlapping components.
- Use addTrace for routing wires with specific points.
"""
        else:
            system_context = common_instructions + """
You are the General Viora EDA Co-pilot. You handle synthesis, subcircuits, and general questions.
- Use synthesize_subcircuit for model creation.
- Use generate_schematic_from_netlist for boost/buck design requests.
"""
    else:
        system_context = common_instructions + """
    You are FluxAI, an expert Electronic Design Automation (EDA) co-pilot.
    You have access to a set of tools to interact with a circuit simulator and research electronic components.

    STRATEGY (MANDATORY):
    1. ZERO-REPL: NEVER write markdown code blocks like ```python plot_signal(...)``` in your response text. This bypasses the system tools.
    2. TOOL-FIRST: Use tools for ANY action. Every action you take MUST be a tool call to trigger the Activity Dashboard.
    3. Audit Transparency: When calling a tool, use <ACTION> tags to explain what you are doing. Every tool call will be logged in the Structured Activity Dashboard.
    4. Discovery: If you are unsure about component names or nodes, ALWAYS call list_nodes() first.
    5. Symbol Library: Use exact schematic component names.
    6. Professionalism: Be precise and concise. Avoid technical filler like "Context attached".

    RESPONSE STRUCTURE:
    1) Result (value + unit)
    2) Scope (target + time window analyzed)
    3) Assumptions (e.g. "Used DC Operating Point")
    4) Command snippets: If you want to trigger interactive plots or workspace actions, wrap the JSON in <SNIPPET>{"commands": ["command_string"]}</SNIPPET> tags.
    5) Suggestions (MANDATORY follow-up buttons using <SUGGESTION>Label|command</SUGGESTION> tag)
    """
    extra_context = ""
    if project_path and ProjectContextRetriever:
        retriever = ProjectContextRetriever(project_path)
        pc = retriever.get_project_context()
        if pc:
            extra_context = f"\n\nProject-Wide Multi-Sheet Context (other sheets):\n{pc}"

    # Final merged system prompt
    core_system_identity = f"You are FluxAI, an expert Electronic Design Automation (EDA) co-pilot. Mode: {mode.upper()}.\n"
    
    context_str = f"\n=== SCHEMATIC CONTEXT (Current Sheet) ===\n{context}\n" if context else ""
    instructions_str = f"\n=== CUSTOM USER INSTRUCTIONS ===\n{instructions}\n" if instructions else ""
    extra_context_str = f"\n=== PROJECT-WIDE CONTEXT (Other Sheets) ===\n{extra_context}\n" if extra_context else ""
    
    # The absolute master instructions that MUST NOT be ignored
    master_formatting_directives = """
=== MASTER FORMATTING DIRECTIVES (STRICT COMPLIANCE REQUIRED) ===
1. TOOL-CALLING: For ALL technical actions (plotting images, listing nodes, etc), you MUST use the provided tool functions first.
2. NO-RAW-JSON: NEVER output raw JSON commands or snippets in your response text. 
3. SNIPPET-TAGS: Wrap any manual Workspace/Simulation commands in <SNIPPET>{"commands": ["cmd1", "cmd2"]}</SNIPPET> tags.
4. ACTION-TAGS: Use <ACTION>Description</ACTION> before starting a tool call to update the UI status.
5. SUGGESTIONS: Always provide at least 2 relevant <SUGGESTION>Label|command</SUGGESTION> tags at the very end of your response for common follow-up actions (e.g. "Plot NODE", "Run Sim").
6. PLOTTING: Use the `plot_signal` tool for static images in the chat. Use <SUGGESTION>Plot NODE|plot signal:NODE</SUGGESTION> or <SNIPPET>{"commands": ["plot signal:NODE"]}</SNIPPET> for opening an interactive oscilloscope window.
7. SUBCIRCUITS: When the user asks you to synthesize or create a custom subcircuit/model, ALWAYS use the `synthesize_subcircuit` tool. After it returns the file path, you MUST output a snippet: <SNIPPET>{"commands": ["import_subckt <absolute_file_path>"]}</SNIPPET> so the UI can pop up the import dialog!
"""

    if mode != "subcircuit":
        full_system_prompt = (
            core_system_identity +
            common_instructions +
            system_context +
            instructions_str +
            context_str +
            extra_context_str +
            master_formatting_directives
        )
    else:
        full_system_prompt = system_context

    if mode == "subcircuit":
        model_name = "gemini-2.0-flash"
    else:
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

    user_parts = [types.Part.from_text(text=prompt)]
    if image_base64:
        try:
            user_parts.append(types.Part.from_bytes(data=base64.b64decode(image_base64), mime_type="image/png"))
        except: pass
    contents.append(types.Content(role="user", parts=user_parts))

    # Prepare tools
    tools_config = []
    registry = None
    if mode != "subcircuit" and project_path and ToolRegistry and get_tools_schema:
        from ai_pipeline.ai_tools.tools import get_subagent_tools
        registry = ToolRegistry(project_path)
        
        # Decide which tool schema to use based on intent
        if mode == "schematic":
            # intent was defined in the mode=="schematic" branch above
            subagent_tools = get_subagent_tools(intent)
        else:
            subagent_tools = get_tools_schema()
            
        tools_config = [types.Tool(function_declarations=subagent_tools)]

    # Streaming tool calling loop
    for _ in range(5):
        try:
            config_params = {
                "tools": tools_config,
                "system_instruction": full_system_prompt
            }
            # Only certain models support thinking
            if "thinking" in model_name.lower():
                config_params["thinking_config"] = types.ThinkingConfig(include_thoughts=True)

            stream = client.models.generate_content_stream(
                model=model_name,
                contents=contents,
                config=types.GenerateContentConfig(**config_params)
            )
            
            full_response_content = None
            final_usage = None
            
            accumulated_text = ""
            tool_calls = []
            tool_call_parts = []
            
            for chunk in stream:
                if not chunk.candidates:
                    continue
                
                # Capture final usage metadata
                if hasattr(chunk, 'usage_metadata') and chunk.usage_metadata:
                    final_usage = {
                        "prompt_tokens": getattr(chunk.usage_metadata, 'prompt_token_count', 0),
                        "candidates_tokens": getattr(chunk.usage_metadata, 'candidates_token_count', 0),
                        "total_tokens": getattr(chunk.usage_metadata, 'total_token_count', 0)
                    }

                for part in chunk.candidates[0].content.parts:
                    # Handle thinking process
                    if hasattr(part, 'thought') and part.thought:
                        print(f"<THOUGHT>{part.thought}</THOUGHT>", end="", flush=True)
                    
                    # Handle text response
                    if part.text:
                        accumulated_text += part.text
                        cleaned = re.sub(r"(?i)(?:^|\s)[\u25c8*•\-]*\s*context attached\b", " ", part.text)
                        print(cleaned, end="", flush=True)
                    
                    # Handle function calls (tools)
                    if part.function_call:
                        tool_calls.append(part.function_call)
                        tool_call_parts.append(part)

            # Output final usage metadata ONCE after the stream is fully finished
            if final_usage:
                print(f"<USAGE>{json.dumps(final_usage)}</USAGE>", end="", flush=True)

            if not tool_calls:
                return

            # Accumulate full history item safely
            model_parts = []
            if accumulated_text:
                model_parts.append(types.Part.from_text(text=accumulated_text))
            model_parts.extend(tool_call_parts)
            contents.append(types.Content(role="model", parts=model_parts))
            
            tool_results_parts = []
            for call in tool_calls:
                # Output structured data for the UI dashboard
                print(f"<TOOL_CALL>{json.dumps({'name': call.name, 'args': call.args})}</TOOL_CALL>", end="", flush=True)
                
                func = getattr(registry, call.name, None) if registry else None
                result = func(**call.args) if func else {"error": "Tool not available"}
                
                # Output structured result
                # Note: We might want to truncate extremely large results (like base64 plots) for the dashboard itself
                dashboard_result = result
                if isinstance(result, dict) and "plot_png_base64" in result:
                    dashboard_result = result.copy()
                    dashboard_result["plot_png_base64"] = "[IMAGE DATA]"
                
                print(f"<TOOL_RESULT>{json.dumps({'name': call.name, 'result': dashboard_result})}</TOOL_RESULT>", end="", flush=True)
                
                tool_results_parts.append(types.Part.from_function_response(name=call.name, response=result))
            
            contents.append(types.Content(role="user", parts=tool_results_parts))
            
        except Exception as e:
            print(f"\nError during generation: {e}", file=sys.stderr)
            sys.exit(1)

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
    parser.add_argument("--instructions", help="Custom system instructions", default="")
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

    generate(args.prompt, args.context, args.mode, args.history, args.instructions, args.image, args.project_path, args.retries, args.model)
