class SmartSignal:
    def init(self):
        # Parameters visible in the IDE
        self.params = {
            "amplitude": 1.0,
            "frequency": 1000.0,
            "offset": 0.0,
            "phase": 0.0
        }

    def update(self, t, inputs):
        import math
        f = self.params["frequency"]
        a = self.params["amplitude"]
        off = self.params["offset"]
        phi = self.params["phase"]
        
        # Standard sine wave formula: A * sin(2*pi*f*t + phi) + offset
        val = a * math.sin(2 * math.pi * f * t + phi) + off
        
        return {"out": val}
