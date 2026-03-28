import asyncio
import os
import sys
import json
import typing
import warnings
from google.genai import types  # pyre-ignore[21]

# Suppress ADK experimental warning for in-memory credential service as early as possible.
# This must run before importing ADK modules because some warnings are emitted at import time.
warnings.filterwarnings(
    "ignore",
    message=r"(?i).*InMemoryCredentialService.*experimental.*",
    category=UserWarning,
)
warnings.filterwarnings(
    "ignore",
    message=r".*",
    category=UserWarning,
    module=r"google\.adk\.auth\.credential_service\.in_memory_credential_service",
)
warnings.filterwarnings(
    "ignore",
    message=r"(?i).*non-text parts in the response.*function_call.*",
    category=UserWarning,
)

# Add ai_pipeline to path
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

try:
    from google.adk import Agent, Runner  # pyre-ignore[21]
    from google.adk.tools import FunctionTool  # pyre-ignore[21]
    from google.adk.sessions import InMemorySessionService  # pyre-ignore[21]
    from google.adk.auth.credential_service.in_memory_credential_service import InMemoryCredentialService  # pyre-ignore[21]
except ImportError as e:
    print(json.dumps({"error": f"Import error: {str(e)}"}), file=sys.stderr)
    sys.exit(1)

try:
    from ai_pipeline.ai_tools.tools import ToolRegistry  # pyre-ignore[21]
except ImportError as e:
    print(json.dumps({"error": f"Could not import ToolRegistry: {str(e)}"}), file=sys.stderr)
    sys.exit(1)

def get_tools(registry):
    """Returns a list of tools wrapped for ADK."""
    async def list_nodes():
        """Lists all available nodes and signals in the current circuit."""
        return registry.list_nodes()

    async def get_signal_data(signal_name: str, analysis_type: str = "tran", stop_time: str = "10m", step_size: str = "100u"):
        """Retrieves simulation data summary for a specific signal."""
        return registry.get_signal_data(signal_name, analysis_type, stop_time, step_size)

    async def compute_average_power(target: str, t_start: float | None = None, t_end: float | None = None):
        """Computes average power for a target (circuit, component, or node)."""
        return registry.compute_average_power(target, t_start, t_end)

    async def plot_signal(signal_name: str):
        """Generates a visual PNG plot of a voltage or current waveform."""
        return registry.plot_signal(signal_name)

    async def execute_commands(commands_json: str):
        """Executes a batch of high-level schematic manipulation commands."""
        return registry.execute_commands(commands_json)

    return [
        FunctionTool(list_nodes),
        FunctionTool(get_signal_data),
        FunctionTool(compute_average_power),
        FunctionTool(plot_signal),
        FunctionTool(execute_commands),
    ]

async def run_agent(prompt_text, project_path, context="", mode="schematic", model_name="gemini-2.5-flash-lite", output_stream=sys.stdout):
    # 2. Setup Tool Registry
    registry = ToolRegistry(project_path)
    tools = get_tools(registry)

    # 3. Define the Agent
    common_instructions = """
You are Viora AI, an expert Electronic Design Architect Powered by Google Python ADK.
Use <ACTION>Thinking about...</ACTION> tags frequently to show your methodology.
You have access to the full schematic context.

RESPONSE STRUCTURE:
1. Technical analysis.
2. Result (value + unit).
3. Suggestions using <SUGGESTION>Label|command</SUGGESTION>.
"""
    system_instructions = common_instructions
    if mode == "ask":
        system_instructions += "\nMode: ASK. Focus on explaining and analyzing the circuit topology."
    else:
        system_instructions += "\nMode: SCHEMATIC. Focus on design, repair, and simulation."

    agent = Agent(
        name="VioraAI",
        instruction=f"{system_instructions}\n\nContext:\n{context}",
        tools=tools,
        model=model_name
    )

    # 4. Setup Runner
    session_service = InMemorySessionService()
    credential_service = InMemoryCredentialService()
    
    runner = Runner(
        agent=agent,
        session_service=session_service,
        credential_service=credential_service,
        app_name="VioraSpice"
    )

    # 5. Execute and Stream
    try:
        session = await session_service.create_session(user_id="viora-user", app_name="VioraSpice")
        new_msg = types.Content(role="user", parts=[types.Part(text=prompt_text)])
        
        async for _event in runner.run_async(
            user_id="viora-user",
            session_id=session.id,
            new_message=new_msg
        ):
            event = typing.cast(typing.Any, _event)
            if hasattr(event, "thought") and event.thought:  # pyre-ignore[16]
                output_stream.write(f"<THOUGHT>{event.thought}</THOUGHT>")  # pyre-ignore[16]
            
            if hasattr(event, "get_function_calls") and event.get_function_calls():  # pyre-ignore[16]
                for call in event.get_function_calls():  # pyre-ignore[16]
                    output_stream.write(f"<ACTION>Using tool: {call.name}...</ACTION>")
            
            if hasattr(event, "content") and event.content and hasattr(event.content, "parts"):  # pyre-ignore[16]
                for part in event.content.parts:  # pyre-ignore[16]
                    if hasattr(part, "text") and part.text:  # pyre-ignore[16]
                        output_stream.write(part.text)  # pyre-ignore[16]
            
            output_stream.flush()

    except Exception as e:
        sys.stderr.write(f"ADK Runner Error: {str(e)}\n")

if __name__ == "__main__":
    # Parsing Arguments for CLI usage
    def get_arg(flag):
        if flag in sys.argv:
            idx = sys.argv.index(flag)
            if idx + 1 < len(sys.argv):
                return sys.argv[idx + 1]
        return None

    m_prompt = sys.argv[1] if len(sys.argv) > 1 else "Hello!"
    m_context = get_arg("--context") or ""
    m_project_path = get_arg("--project_path") or ""
    m_mode = get_arg("--mode") or "schematic"
    m_model = get_arg("--model") or "gemini-2.5-flash-lite"

    asyncio.run(run_agent(m_prompt, m_project_path, m_context, m_mode, m_model))
