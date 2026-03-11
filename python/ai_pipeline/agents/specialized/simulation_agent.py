from adk.agents import LlmAgent
from ...ai_tools.tools import ToolRegistry
import os

class SimulationAgent:
    """
    An ADK-powered specialized agent for circuit simulation tasks.
    It encapsulates simulation tools and uses an LLM to reason about 
    the best analysis parameters.
    """
    def __init__(self, schematic_path, api_key=None, model_name="gemini-2.0-flash-thinking-exp-01-21", system_instruction=""):
        self.schematic_path = schematic_path
        self.api_key = api_key or os.environ.get("GEMINI_API_KEY")
        self.model_name = model_name
        self.registry = ToolRegistry(self.schematic_path)
        
        default_instruction = (
            "You are an expert Simulation Engineer. Your goal is to answer technical questions "
            "about circuit behavior using the provided tools. Always try to list nodes if unsure. "
            "Use sensible defaults for simulation (e.g. 10ms transient for power)."
        )
        
        # Initialize the ADK LlmAgent
        self.agent = LlmAgent(
            name="SimulationExpert",
            description="Expert in circuit simulation, power analysis, and signal discovery.",
            system_instruction=system_instruction or default_instruction,
            model=self.model_name,
            api_key=self.api_key
        )
        
        # Register the tools with ADK
        self.agent.register_tool(self.registry.list_nodes)
        self.agent.register_tool(self.registry.get_signal_data)
        self.agent.register_tool(self.registry.compute_average_power)
        self.agent.register_tool(self.registry.run_simulation)
        self.agent.register_tool(self.registry.plot_signal)
        self.agent.register_tool(self.registry.generate_snippet)

    def run(self, prompt, history=None):
        """
        Executes the simulation request using ADK orchestration.
        Returns the final text response.
        """
        # Note: In a real implementation, we would handle ADK's internal thought streaming here.
        # For now, we use its built-in run method which handles the tool-calling loop.
        response = self.agent.run(prompt, history=history)
        return response.text
