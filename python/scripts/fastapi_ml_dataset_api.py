#!/usr/bin/env python3
import argparse
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
PYTHON_ROOT = REPO_ROOT / "python"
if str(PYTHON_ROOT) not in sys.path:
    sys.path.insert(0, str(PYTHON_ROOT))


def main() -> None:
    parser = argparse.ArgumentParser(description="VioSpice ML dataset FastAPI service")
    parser.add_argument("--host", default="0.0.0.0", help="Host to bind")
    parser.add_argument("--port", type=int, default=8790, help="Port to bind")
    parser.add_argument("--cli-path", help="Explicit path to vio-cmd or flux-cmd")
    parser.add_argument("--job-store", help="Path to persistent async job store JSON file")
    parser.add_argument("--reload", action="store_true", help="Enable uvicorn reload mode")
    args = parser.parse_args()

    try:
        import uvicorn
    except ImportError as exc:
        raise SystemExit("uvicorn is not installed. Add FastAPI dependencies from python/requirements.txt.") from exc

    from ai_pipeline.api.fastapi_ml_dataset_api import create_app

    uvicorn.run(
        create_app(args.cli_path, args.job_store),
        host=args.host,
        port=args.port,
        reload=args.reload,
    )


if __name__ == "__main__":
    main()
