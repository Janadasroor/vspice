class SmartSignal:
    def init(self):
        self.params = {
            "frequency": 500.0,
            "duty_cycle": 0.5,  # 0.0 to 1.0
            "v_high": 5.0,
            "v_low": 0.0
        }

    def update(self, t, inputs):
        f = self.params["frequency"]
        dc = self.params["duty_cycle"]
        vh = self.params["v_high"]
        vl = self.params["v_low"]
        
        # Period T = 1/f
        period = 1.0 / f
        # Current position in the cycle
        phase = (t % period) / period
        
        val = vh if phase < dc else vl
        return {"out": val}
