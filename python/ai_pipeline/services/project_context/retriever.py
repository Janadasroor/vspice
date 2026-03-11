import os
import glob
import json

class ProjectContextRetriever:
    def __init__(self, current_file_path):
        self.current_file_path = current_file_path
        
    def get_project_context(self):
        """
        Scans the project directory for other schematic sheets (.flxsch) 
        and extracts a summary of components and key nets.
        This provides multi-sheet awareness to the AI.
        """
        if not self.current_file_path or not os.path.exists(self.current_file_path):
            return None
            
        project_dir = os.path.dirname(os.path.abspath(self.current_file_path))
        
        # Find all schematic files in the directory
        sch_files = glob.glob(os.path.join(project_dir, "*.flxsch"))
        
        # Exclude the current file as it's already in the main context
        sch_files = [f for f in sch_files if os.path.abspath(f) != os.path.abspath(self.current_file_path)]
        
        if not sch_files:
            return None
            
        project_summary = {}
        for f in sch_files:
            filename = os.path.basename(f)
            try:
                with open(f, 'r', encoding='utf-8') as file:
                    data = json.load(file)
                    items = data.get("items", [])
                    
                    # Summarize components
                    components = [
                        item for item in items 
                        if "reference" in item and item.get("type") not in ("Wire", "Junction", "Net Label", "GND", "VCC", "Label", "Sheet")
                    ]
                    comp_summary = [f"{c.get('reference')} ({c.get('value') or c.get('type')})" for c in components]
                    
                    # Summarize nets
                    nets = [
                        item for item in items 
                        if item.get("type") in ("Net Label", "GND", "VCC", "Global Label")
                    ]
                    net_summary = list(set([n.get("text", n.get("value", "")) for n in nets if n.get("text") or n.get("value")]))
                    
                    project_summary[filename] = {
                        "components": comp_summary,
                        "labeled_nets": net_summary
                    }
            except Exception as e:
                project_summary[filename] = {"error": f"Failed to parse: {str(e)}"}
                
        return json.dumps(project_summary, indent=2)
