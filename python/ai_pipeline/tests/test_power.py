import unittest
import numpy as np
import os
import sys

# Ensure parent directory is in path for relative imports if run as script
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '../../..')))

from python.ai_pipeline.services.power_metrics.compute import compute_average_power

class TestPowerMetrics(unittest.TestCase):
    def test_constant_power(self):
        t = [0, 1, 2]
        v = [5, 5, 5]
        i = [2, 2, 2]
        # P = 10W
        self.assertAlmostEqual(compute_average_power(t, v, i), 10.0)

    def test_linear_power(self):
        t = [0, 1, 2]
        v = [0, 5, 10]
        i = [1, 1, 1]
        # P(t) = 5t
        # Avg P = (1/2) * integral_0^2 5t dt = (1/2) * [2.5t^2]_0^2 = (1/2) * 10 = 5W
        self.assertAlmostEqual(compute_average_power(t, v, i), 5.0)

    def test_windowed_power(self):
        t = np.linspace(0, 10, 11)
        v = np.ones(11) * 10
        i = np.ones(11) * 2
        # P = 20W everywhere
        # Window 2 to 8
        self.assertAlmostEqual(compute_average_power(t, v, i, t_start=2, t_end=8), 20.0)

    def test_invalid_dimensions(self):
        with self.assertRaises(ValueError):
            compute_average_power([0, 1], [1], [1])

if __name__ == '__main__':
    unittest.main()
