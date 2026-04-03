from typing import Any, Dict, List, Optional

from fastapi import FastAPI, HTTPException
from pydantic import BaseModel, Field

from .ml_dataset_api import SimulationDatasetService, VioCmdRunner, _utc_now_iso


class _BasePayloadModel(BaseModel):
    class Config:
        extra = "allow"


class SimulateRequest(_BasePayloadModel):
    schematic_path: str
    analysis: Optional[str] = "tran"
    stop: Optional[str] = None
    step: Optional[str] = None
    signals: List[str] = Field(default_factory=list)
    measures: List[str] = Field(default_factory=list)
    labels: Dict[str, Any] = Field(default_factory=dict)
    tags: Dict[str, Any] = Field(default_factory=dict)
    metadata: Dict[str, Any] = Field(default_factory=dict)
    derived_labels: List[Dict[str, Any]] = Field(default_factory=list)
    result_filters: List[Dict[str, Any]] = Field(default_factory=list)
    discard_filtered: bool = False


class BatchRequest(_BasePayloadModel):
    jobs: List[Dict[str, Any]]
    concurrency: int = 4
    output_path: Optional[str] = None
    fail_fast: bool = False
    inline_results: bool = False


class SweepRequest(_BasePayloadModel):
    template_schematic_path: str
    parameters: List[Dict[str, Any]]
    job_template: Dict[str, Any] = Field(default_factory=dict)
    variant_dir: Optional[str] = None
    sampling: Dict[str, Any] = Field(default_factory=dict)
    split_ratios: Dict[str, Any] = Field(default_factory=dict)
    constraints: List[Dict[str, Any]] = Field(default_factory=list)
    concurrency: int = 4
    output_path: Optional[str] = None
    fail_fast: bool = False
    inline_results: bool = False


def create_app(cli_path: Optional[str] = None) -> FastAPI:
    service = SimulationDatasetService(VioCmdRunner(cli_path))
    app = FastAPI(
        title="VioSpice ML Dataset API",
        version="0.2.0",
        description="ASGI/OpenAPI wrapper for VioSpice ML-oriented simulation, batch generation, and sweep dataset workflows.",
    )

    @app.get("/api/ml/health")
    def health() -> Dict[str, Any]:
        return {
            "ok": True,
            "service": "viospice-ml-dataset-api-fastapi",
            "created_at": _utc_now_iso(),
            "cli_path": service.runner.cli_path,
        }

    @app.post("/api/ml/simulate")
    def simulate(request: SimulateRequest) -> Dict[str, Any]:
        try:
            sample = service.run_job(request.dict())
        except Exception as exc:
            raise HTTPException(status_code=400, detail=str(exc)) from exc
        return {"ok": sample.get("ok", False), "sample": sample}

    @app.post("/api/ml/batch")
    def batch(request: BatchRequest) -> Dict[str, Any]:
        try:
            return service.run_batch(
                jobs=request.jobs,
                concurrency=request.concurrency,
                output_path=request.output_path,
                fail_fast=request.fail_fast,
                inline_results=request.inline_results,
            )
        except Exception as exc:
            raise HTTPException(status_code=400, detail=str(exc)) from exc

    @app.post("/api/ml/sweep")
    def sweep(request: SweepRequest) -> Dict[str, Any]:
        try:
            return service.run_sweep(request.dict())
        except Exception as exc:
            raise HTTPException(status_code=400, detail=str(exc)) from exc

    return app


app = create_app()
