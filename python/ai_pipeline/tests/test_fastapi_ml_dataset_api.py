import importlib.util
import sys
import unittest
from pathlib import Path


PYTHON_ROOT = Path(__file__).resolve().parents[2]
if str(PYTHON_ROOT) not in sys.path:
    sys.path.insert(0, str(PYTHON_ROOT))


FASTAPI_AVAILABLE = bool(importlib.util.find_spec("fastapi")) and bool(importlib.util.find_spec("pydantic"))


@unittest.skipUnless(FASTAPI_AVAILABLE, "fastapi not installed")
class FastApiMlDatasetApiTest(unittest.TestCase):
    def test_create_app_exposes_expected_routes(self):
        from ai_pipeline.api.fastapi_ml_dataset_api import create_app

        app = create_app(cli_path="build/vio-cmd")
        paths = {route.path for route in app.routes}
        self.assertIn("/api/ml/health", paths)
        self.assertIn("/api/ml/simulate", paths)
        self.assertIn("/api/ml/batch", paths)
        self.assertIn("/api/ml/sweep", paths)


if __name__ == "__main__":
    unittest.main()
