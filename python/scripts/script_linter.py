import ast
import sys
import json

def lint_script(code):
    errors = []
    try:
        tree = ast.parse(code)
    except SyntaxError as e:
        errors.append({"line": e.lineno, "msg": f"Syntax Error: {e.msg}"})
        return json.dumps(errors)

    # Disallowed modules for security and stability
    forbidden_imports = {'os', 'sys', 'shutil', 'subprocess', 'socket', 'requests'}
    
    for node in ast.walk(tree):
        # Check for forbidden imports
        if isinstance(node, ast.Import):
            for alias in node.names:
                if alias.name in forbidden_imports:
                    errors.append({"line": node.lineno, "msg": f"Security Error: Import of '{alias.name}' is forbidden."})
        if isinstance(node, ast.ImportFrom):
            if node.module in forbidden_imports:
                errors.append({"line": node.lineno, "msg": f"Security Error: Import from '{node.module}' is forbidden."})
        
        # Check for potential infinite loops (simple check)
        if isinstance(node, ast.While):
            if isinstance(node.test, ast.Constant) and node.test.value == True:
                errors.append({"line": node.lineno, "msg": "Stability Error: 'while True' detected."})

    return json.dumps(errors if errors else [])

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(json.dumps([]))
        sys.exit(0)
    print(lint_script(sys.argv[1]))
