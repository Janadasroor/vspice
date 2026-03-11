import sys
import os
import ast
import json

def python_to_spice_expression(expr_node):
    """
    Attempts to convert a Python AST expression node to a SPICE B-source string.
    """
    if isinstance(expr_node, ast.BinOp):
        left = python_to_spice_expression(expr_node.left)
        right = python_to_spice_expression(expr_node.right)
        op_map = {
            ast.Add: "+",
            ast.Sub: "-",
            ast.Mult: "*",
            ast.Div: "/",
            ast.Pow: "**"
        }
        op = op_map.get(type(expr_node.op), None)
        if op and left and right:
            return f"({left} {op} {right})"
    
    if isinstance(expr_node, ast.Constant):
        return str(expr_node.value)
    
    if isinstance(expr_node, ast.Name):
        # Map variables to SPICE net names or parameters
        return f"V({expr_node.id})"
    
    if isinstance(expr_node, ast.Subscript):
        # Handle inputs.get('In1') or inputs['In1']
        if isinstance(expr_node.value, ast.Name) and expr_node.value.id == "inputs":
            if isinstance(expr_node.slice, ast.Constant):
                return f"V({expr_node.slice.value})"
    
    if isinstance(expr_node, ast.Call):
        # Handle math functions: math.sin -> sin
        func_name = ""
        if isinstance(expr_node.func, ast.Attribute):
            func_name = expr_node.func.attr
        elif isinstance(expr_node.func, ast.Name):
            func_name = expr_node.func.id
            
        args = [python_to_spice_expression(a) for i, a in enumerate(expr_node.args)]
        if all(args):
            return f"{func_name}({', '.join(args)})"

    return None

def bake_to_spice(code, block_name, inputs, outputs):
    """
    Analyzes the Python SmartSignal code and generates a .SUBCKT
    """
    try:
        tree = ast.parse(code)
    except Exception as e:
        return f"* Error parsing Python: {str(e)}"

    update_node = None
    params = {}
    
    # 1. Find SmartSignal.update and .init
    for node in ast.walk(tree):
        if isinstance(node, ast.FunctionDef):
            if node.name == "update":
                update_node = node
            elif node.name == "init":
                # Try to extract self.params = { ... }
                for sub in node.body:
                    if isinstance(sub, ast.Assign):
                        for target in sub.targets:
                            if isinstance(target, ast.Attribute) and target.attr == "params":
                                if isinstance(sub.value, ast.Dict):
                                    for k, v in zip(sub.value.keys(), sub.value.values()):
                                        if isinstance(k, ast.Constant) and isinstance(v, (ast.Constant, ast.UnaryOp)):
                                            params[k.value] = ast.literal_eval(v)

    if not update_node:
        return "* Error: 'update' method not found"

    # 2. Extract return expression
    # We look for 'return {"out": ...}' or 'return val'
    spice_expr = "0"
    for sub in update_node.body:
        if isinstance(sub, ast.Return):
            if isinstance(sub.value, ast.Dict):
                # Multiple outputs case
                pass 
            else:
                spice_expr = python_to_spice_expression(sub.value) or "0"

    # 3. Build the SUBCKT
    subckt = f"* SPICE BAKE: {block_name}
"
    subckt += f".SUBCKT {block_name} {' '.join(inputs)} {' '.join(outputs)} GND
"
    
    # Add parameters
    for k, v in params.items():
        subckt += f".PARAM {k}={v}
"
        
    # Standard SPICE constants/aliases
    subckt += ".FUNC pi() {3.14159265359}
"
    
    # The Behavioral Source
    # For now assuming single output 'out'
    primary_out = outputs[0] if outputs else "out"
    subckt += f"B1 {primary_out} GND V={spice_expr}
"
    
    subckt += ".ENDS
"
    return subckt

if __name__ == "__main__":
    if len(sys.argv) < 5:
        sys.exit(1)
        
    code = sys.argv[1]
    name = sys.argv[2]
    inputs = json.loads(sys.argv[3])
    outputs = json.loads(sys.argv[4])
    
    print(bake_to_spice(code, name, inputs, outputs))
