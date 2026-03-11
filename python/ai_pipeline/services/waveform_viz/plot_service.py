import matplotlib.pyplot as plt
import io
import base64

class PlotService:
    @staticmethod
    def plot_waveform(x, y, signal_name, title=None, xlabel="Time (s)", ylabel="Amplitude"):
        """
        Generates a PNG plot of a waveform and returns it as a base64 encoded string.
        Optimized for a dark-themed UI.
        """
        if not x or not y or len(x) != len(y):
            return None

        # Create a stylized dark-themed plot
        plt.style.use('dark_background')
        fig, ax = plt.subplots(figsize=(8, 4), dpi=100)
        
        # Professional color palette (slate/blue)
        color = '#3b82f6' 
        if signal_name.startswith('V'): color = '#60a5fa' # Voltage: Light Blue
        elif signal_name.startswith('I'): color = '#fbbf24' # Current: Amber
        
        ax.plot(x, y, color=color, linewidth=2, label=signal_name)
        
        # Grid and labels
        ax.grid(True, linestyle='--', alpha=0.3, color='#444444')
        ax.set_title(title or f"Signal Trace: {signal_name}", color='#eeeeee', pad=15, fontweight='bold')
        ax.set_xlabel(xlabel, color='#888888')
        ax.set_ylabel(ylabel, color='#888888')
        ax.tick_params(colors='#666666', labelsize=9)
        
        # Add a subtle background glow/fill
        ax.fill_between(x, y, color=color, alpha=0.1)
        
        plt.tight_layout()

        # Save to buffer
        buf = io.BytesIO()
        plt.savefig(buf, format='png', bbox_inches='tight', transparent=True)
        buf.seek(0)
        img_base64 = base64.b64encode(buf.read()).decode('utf-8')
        plt.close(fig)
        
        return img_base64
