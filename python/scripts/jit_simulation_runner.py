def run_jit_preview(code, t_start, t_end, t_step, params):
    """
    Compiles and runs the user code over a time range.
    Uses numba for JIT acceleration if possible.
    """
    import numpy as np
    import math
    
    try:
        import numba
        has_numba = True
    except ImportError:
        has_numba = False

    namespace = {
        'np': np,
        'math': math,
        'numpy': np
    }
    try:
        exec(code, namespace)
    except Exception as e:
        return None, None, f"Execution Error: {str(e)}"
    
    if 'SmartSignal' not in namespace:
        return None, None, "Error: SmartSignal class not found"
    
    cls = namespace['SmartSignal']
    instance = cls()
    if hasattr(instance, 'init'):
        instance.init()
    
    # Merge UI params
    if hasattr(instance, 'params'):
        for k, v in params.items():
            instance.params[k] = v
            
    t_array = np.arange(t_start, t_end, t_step)
    results = np.zeros(len(t_array))
    
    # --- JIT Compilation Strategy ---
    # We attempt to create a specialized JIT function for this specific update logic.
    
    if has_numba:
        # We define a wrapper that numba can optimize
        # Note: This is an 'object mode' JIT which is still faster than pure Python
        @numba.jit(forceobj=True)
        def fast_loop(t_arr, inst):
            out = np.zeros(len(t_arr))
            for i in range(len(t_arr)):
                res = inst.update(t_arr[i], {})
                if isinstance(res, dict):
                    out[i] = res.get('out', 0.0)
                else:
                    out[i] = float(res)
            return out

        try:
            results = fast_loop(t_array, instance)
            return t_array.tolist(), results.tolist(), None
        except Exception as e:
            # Fallback to standard loop if JIT fails
            pass

    # Standard loop fallback
    try:
        for i in range(len(t_array)):
            res = instance.update(t_array[i], {})
            if isinstance(res, dict):
                results[i] = res.get('out', 0.0)
            else:
                results[i] = float(res)
        return t_array.tolist(), results.tolist(), None
    except Exception as e2:
        return None, None, str(e2)
