class SmartSignal:
    def init(self):
        self.params = {
            "Kp": 1.0,  # Proportional gain
            "Ki": 0.1,  # Integral gain
            "Kd": 0.01, # Derivative gain
            "setpoint": 1.0
        }
        self.integral = 0.0
        self.last_error = 0.0
        self.last_t = 0.0

    def update(self, t, inputs):
        # Error = Setpoint - ProcessValue (feedback from pin In1)
        pv = inputs.get("In1", 0.0)
        sp = self.params["setpoint"]
        error = sp - pv
        
        # Simple trapezoidal integration
        dt = t - self.last_t if self.last_t > 0 else 0.0001
        self.integral += error * dt
        
        # Derivative (rate of change)
        derivative = (error - self.last_error) / dt if dt > 0 else 0.0
        
        # PID Output
        kp = self.params["Kp"]
        ki = self.params["Ki"]
        kd = self.params["Kd"]
        val = (kp * error) + (ki * self.integral) + (kd * derivative)
        
        # State update for next step
        self.last_error = error
        self.last_t = t
        
        return {"out": val}
