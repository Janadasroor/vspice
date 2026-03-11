#!/usr/bin/env python3
import sys
import os
import json
import argparse
from google import genai
from google.genai import types

# SPICE Parameter Definitions (from SpiceModelArchitect.cpp)
DEVICE_PARAMS = {
    "Diode (D)": ["IS", "RS", "N", "CJO", "VJ", "M", "TT", "BV", "IBV"],
    "NPN BJT (NPN)": ["IS", "BF", "NF", "VAF", "IKF", "RB", "RE", "RC", "CJE", "CJC"],
    "Power MOSFET (VDMOS)": ["VTO", "KP", "RG", "RD", "RS", "LAMBDA", "MTRIODE", "KSUBTHRES", "CGDMAX", "CGDMIN", "CGS", "CJO", "IS", "RB", "Ron", "Qg"],
    "NMOS (NMOS)": ["VTO", "KP", "GAMMA", "PHI", "LAMBDA", "RD", "RS", "CGSO", "CGDO"]
}

def extract_params(api_key, pdf_path, device_type):
    client = genai.Client(api_key=api_key)
    
    if device_type not in DEVICE_PARAMS:
        return {"error": f"Unsupported device type: {device_type}"}
    
    target_params = DEVICE_PARAMS[device_type]
    
    # Upload the PDF
    try:
        with open(pdf_path, "rb") as f:
            pdf_data = f.read()
    except Exception as e:
        return {"error": f"Failed to read PDF: {str(e)}"}

    prompt = f"""
    You are an expert SPICE modeling engineer. Your task is to analyze the provided datasheet PDF for a {device_type}.
    Extract the key electrical characteristics and map them to the following SPICE model parameters:
    {', '.join(target_params)}

    Requirements:
    1. If multiple values (min, typ, max) are given, prioritize 'Typical' values.
    2. Convert units to standard SPICE units (e.g., Volts, Amperes, Ohms, Farads).
    3. If a parameter cannot be found or estimated from the datasheet, omit it from the JSON.
    4. Return ONLY a JSON object mapping the parameter names to their extracted values (as strings or numbers).
    """

    try:
        # Using Gemini 1.5 Flash for speed and cost efficiency
        response = client.models.generate_content(
            model="gemini-1.5-flash",
            contents=[
                types.Part.from_bytes(data=pdf_data, mime_type="application/pdf"),
                prompt
            ],
            config=types.GenerateContentConfig(
                response_mime_type="application/json"
            )
        )
        
        return json.loads(response.text)
    except Exception as e:
        return {"error": f"AI extraction failed: {str(e)}"}

def main():
    parser = argparse.ArgumentParser(description='Extract SPICE parameters from a datasheet PDF.')
    parser.add_argument('--pdf', required=True, help='Path to the PDF datasheet')
    parser.add_argument('--type', required=True, help='Device category (e.g., "Power MOSFET (VDMOS)")')
    parser.add_argument('--api_key', default=os.environ.get("GEMINI_API_KEY"), help='Gemini API Key')
    
    args = parser.parse_args()
    
    if not args.api_key:
        print(json.dumps({"error": "GEMINI_API_KEY not found. Please set it as an environment variable or pass it via --api_key."}))
        sys.exit(1)
        
    result = extract_params(args.api_key, args.pdf, args.type)
    print(json.dumps(result))

if __name__ == "__main__":
    main()
