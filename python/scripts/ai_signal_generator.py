import sys
import os
from google import genai
from google.genai import types

def generate_signal_logic(prompt, api_key):
    client = genai.Client(api_key=api_key)
    
    system_instruction = """
    You are an EDA AI assistant. Generate a Python class named 'SmartSignal' 
    for a behavioral signal block. 
    The class must have an 'update(self, t, inputs)' method.
    't' is simulation time in seconds.
    'inputs' is a dictionary of pin voltages.
    Return only the Python code, no markdown, no explanations.
    """
    
    try:
        response = client.models.generate_content(
            model="gemini-2.0-flash",
            config=types.GenerateContentConfig(
                system_instruction=system_instruction,
                temperature=0.1
            ),
            contents=[prompt]
        )
        return response.text.strip()
    except Exception as e:
        return f"# Error: {str(e)}"

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("# Error: Missing arguments")
        sys.exit(1)
        
    user_prompt = sys.argv[1]
    api_key = sys.argv[2]
    
    print(generate_signal_logic(user_prompt, api_key))
