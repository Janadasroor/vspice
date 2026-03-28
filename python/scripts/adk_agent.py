import asyncio
import os
import sys
import json
import typing
import warnings
from collections import deque
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
    from ai_pipeline.services.memory_store import clear_memory, forget_fact, format_memory_context, remember_fact, render_memory_report, update_memory  # pyre-ignore[21]
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


def _handle_memory_command(prompt_text, project_path):
    text = str(prompt_text or "").strip()
    lower = text.lower()

    if lower in {"show memory", "memory", "view memory"}:
        return render_memory_report(project_path)

    if lower in {"clear memory", "reset memory"}:
        clear_memory(project_path)
        return "Project memory was cleared."

    if lower.startswith("remember "):
        payload = text[9:].strip()
        bucket = "project_notes"
        if payload.lower().startswith("preference:"):
            payload = payload.split(":", 1)[1].strip()
            bucket = "user_preferences"
        ok, message = remember_fact(project_path, payload, bucket=bucket)
        return message if ok else f"Memory command failed: {message}"

    if lower.startswith("forget "):
        payload = text[7:].strip()
        ok, message = forget_fact(project_path, payload)
        return message if ok else f"Memory command failed: {message}"

    return None

async def run_agent(prompt_text, project_path, context="", mode="schematic", model_name="gemini-2.5-flash-lite", history_json="", output_stream=sys.stdout):
    memory_command_response = _handle_memory_command(prompt_text, project_path)
    if memory_command_response is not None:
        output_stream.write(memory_command_response)
        output_stream.flush()
        return

    # 2. Setup Tool Registry
    registry = ToolRegistry(project_path)
    tools = get_tools(registry)
    memory_context = format_memory_context(project_path, history_json)

    # 3. Define the Agent
    common_instructions = """
You are Viora AI, an expert Electronic Design Architect Powered by Google Python ADK.
Use <ACTION>Thinking about...</ACTION> tags frequently to show your methodology.
You have access to the full schematic context.

RESPONSE STRUCTURE:
1. Technical analysis.
2. Result (value + unit).
3. Suggestions using <SUGGESTION>Label|command</SUGGESTION>.

TOOL-CALL GUARDRAILS:
- Never repeat the same tool call with the same arguments more than 2 times.
- If tools return no new information twice in a row, stop tool-calling and explain the blocker clearly.
- Avoid alternating loops (example: list_nodes -> get_signal_data -> list_nodes -> get_signal_data).
- After failed attempts, ask at most one concise clarification question.

MEMORY POLICY:
- Treat persistent memory as project-scoped working memory, not ground truth.
- Reuse remembered preferences, notes, and recent decisions when they help.
- If current schematic context conflicts with memory, trust the current context.
"""
    system_instructions = common_instructions
    if mode == "ask":
        system_instructions += "\nMode: ASK. Focus on explaining and analyzing the circuit topology."
    else:
        system_instructions += "\nMode: SCHEMATIC. Focus on design, repair, and simulation."

    full_context = context
    if memory_context:
        full_context = f"{context}\n\nPersistent memory:\n{memory_context}".strip()

    agent = Agent(
        name="VioraAI",
        instruction=f"{system_instructions}\n\nContext:\n{full_context}",
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
        max_tool_calls = 24
        max_same_tool_streak = 4
        max_abab_repeat_window = 8
        tool_call_count = 0
        tool_history = deque(maxlen=32)
        emitted_call_keys: set[str] = set()
        response_chunks = []
        
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
                    call_name = str(getattr(call, "name", "") or "").strip()
                    call_args = getattr(call, "args", None)
                    if isinstance(call_args, dict):
                        args_key = json.dumps(call_args, sort_keys=True, ensure_ascii=False)
                    else:
                        args_key = str(call_args)
                    call_key = f"{call_name}|{args_key}"

                    # De-duplicate repeated function-call events from stream updates.
                    if call_key not in emitted_call_keys:
                        output_stream.write(f"<ACTION>Using tool: {call_name}...</ACTION>")
                        emitted_call_keys.add(call_key)

                    tool_call_count += 1
                    tool_history.append(call_name)

                    # Guard 1: hard maximum tool calls per request.
                    if tool_call_count >= max_tool_calls:
                        output_stream.write(
                            "I stopped because tool-calling exceeded the safe limit for one request. "
                            "This usually means the simulator did not return stable data. "
                            "Please try: run simulation once, then ask for a specific signal like V(Net6) or I(R3)."
                        )
                        output_stream.flush()
                        return

                    # Guard 2: same tool repeated too many times in a row.
                    if len(tool_history) >= max_same_tool_streak:
                        tail = list(tool_history)[-max_same_tool_streak:]
                        if len(set(tail)) == 1:
                            output_stream.write(
                                f"I stopped because `{tail[-1]}` was repeated without progress. "
                                "The netlist or simulation results may be unavailable in this run."
                            )
                            output_stream.flush()
                            return

                    # Guard 3: ABAB loop pattern (A,B,A,B,...), common in discovery loops.
                    if len(tool_history) >= max_abab_repeat_window:
                        tail = list(tool_history)[-max_abab_repeat_window:]
                        if len(set(tail)) == 2 and all(tail[i] == tail[i % 2] for i in range(max_abab_repeat_window)):
                            output_stream.write(
                                "I stopped because the tool calls entered a repeated discovery loop. "
                                "Please provide one exact target signal (example: V(Net6), V(R3:1)-V(R3:2), or I(V1)) "
                                "or run simulation first."
                            )
                            output_stream.flush()
                            return
            
            if hasattr(event, "content") and event.content and hasattr(event.content, "parts"):  # pyre-ignore[16]
                for part in event.content.parts:  # pyre-ignore[16]
                    if hasattr(part, "text") and part.text:  # pyre-ignore[16]
                        response_chunks.append(str(part.text))
                        output_stream.write(part.text)  # pyre-ignore[16]
            
            output_stream.flush()

        update_memory(project_path, prompt_text, "".join(response_chunks).strip())

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
    m_history = get_arg("--history") or ""

    asyncio.run(run_agent(m_prompt, m_project_path, m_context, m_mode, m_model, m_history))
