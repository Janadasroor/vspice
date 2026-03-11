import numpy as np

def compute_average_power(time_array, voltage_array, current_array, t_start=None, t_end=None):
    """
    Computes average power P = integral(V * I dt) / (t_end - t_start).
    Supports time windows via t_start and t_end.
    """
    if len(time_array) < 2 or len(voltage_array) != len(time_array) or len(current_array) != len(time_array):
        raise ValueError("Invalid signal dimensions for power computation.")
    
    t = np.array(time_array)
    v = np.array(voltage_array)
    i = np.array(current_array)
    p = v * i
    
    # Filter by time window if specified
    mask = np.ones(len(t), dtype=bool)
    if t_start is not None:
        mask &= (t >= t_start)
    if t_end is not None:
        mask &= (t <= t_end)
    
    t_win = t[mask]
    p_win = p[mask]
    
    if len(t_win) < 2:
        # Fallback to single point or empty window
        return float(np.mean(p_win)) if len(p_win) > 0 else 0.0
    
    # Numerical integration using trapezoidal rule
    if hasattr(np, 'trapezoid'):
        energy = np.trapezoid(p_win, t_win)
    else:
        energy = np.trapz(p_win, t_win)
    duration = t_win[-1] - t_win[0]
    
    if duration <= 0:
        return float(np.mean(p_win))
        
    return float(energy / duration)
