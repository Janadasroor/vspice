import os
import sys
import json
import argparse
import re
from google import genai
from google.genai import types
from ..ai_tools.tools import ToolRegistry, get_tools_schema

def run_ai_query(prompt, schematic_path, debug=False):
    api_key = os.environ.get("GEMINI_API_KEY")
    if not api_key:
        print("Error: GEMINI_API_KEY environment variable not set.", file=sys.stderr)
        return

    client = genai.Client(api_key=api_key)
    model_name = "gemini-2.0-flash-thinking-exp-01-21"

    octopart_key = os.environ.get("OCTOPART_API_KEY")
    registry = ToolRegistry(schematic_path, octopart_api_key=octopart_key)
    tools = get_tools_schema()

    system_instruction = """
    You are FluxAI, an expert Electronic Design Automation (EDA) co-pilot.
    You have access to a set of tools to interact with a circuit simulator.

    STRATEGY (MANDATORY):
    1. Discovery: If you are unsure about component names or nodes, ALWAYS call list_nodes() first.
    2. Direct Action: If the user asks for power/voltage/current, and you know the target name (e.g. V1, R1, Net1), call the tool immediately. Do not ask for node names unless the tool specifically fails.
    3. Signal Naming: The simulator usually names signals like V(node) or I(component). tools like compute_average_power handle these, but if you query raw data, try both names.
    4. Professionalism: Be precise and concise.

    RESPONSE STRUCTURE:
    1) Result (value + unit)
    2) Scope (target + time window analyzed)
    3) Assumptions (short)
    """

    contents = [types.Content(role="user", parts=[types.Part.from_text(text=prompt)])]
    
    for _ in range(5):
        try:
            response = client.models.generate_content(
                model=model_name,
                contents=contents,
                config=types.GenerateContentConfig(
                    system_instruction=system_instruction,
                    thinking_config=types.ThinkingConfig(include_thoughts=True),
                    tools=[types.Tool(function_declarations=tools)]
                )
            )

            if response.candidates[0].content.parts:
                thought = response.candidates[0].content.parts[0].thought if hasattr(response.candidates[0].content.parts[0], 'thought') else None
                if thought and debug:
                    print(f"--- [THOUGHT]\n{thought}\n---")

            tool_calls = []
            for part in response.candidates[0].content.parts:
                if part.function_call:
                    tool_calls.append(part.function_call)

            if not tool_calls:
                # Final response
                text = response.text
                if text:
                    # Clean up "Context attached" artifacts if any
                    text = re.sub(r"(?i)(?:^|\s)[\u25c8*•\-]*\s*context attached\b", " ", text)
                    text = re.sub(r"\n{3,}", "\n\n", text).strip()
                    print(text)
                return

            contents.append(response.candidates[0].content)

            tool_results_parts = []
            for call in tool_calls:
                if debug:
                    print(f"--- [DEBUG] Tool: {call.name}({call.args})")
                
                func = getattr(registry, call.name, None)
                if func:
                    result = func(**call.args)
                else:
                    result = {"error": f"Tool {call.name} not found."}
                
                if debug:
                    print(f"--- [DEBUG] Result: {result}")

                tool_results_parts.append(
                    types.Part.from_function_response(
                        name=call.name,
                        response=result
                    )
                )
            
            contents.append(types.Content(role="user", parts=tool_results_parts))
        except Exception as e:
            print(f"Error during AI query loop: {e}", file=sys.stderr)
            return

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("prompt", help="The natural language question about the circuit.")
    parser.add_argument("--schematic", required=True, help="Path to the .sch file.")
    parser.add_argument("--debug", action="store_true", help="Enable debug trace.")
    args = parser.parse_args()

    run_ai_query(args.prompt, args.schematic, args.debug)
