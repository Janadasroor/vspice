# ML Engineer Guide

This guide shows how to use the VioSpice ML service to generate simulation datasets for model training.

## What you get

Each dataset record can include:

- schematic source path
- simulation configuration
- generated netlist JSON
- waveform samples
- signal statistics
- measure results
- derived labels
- sweep metadata
- acceptance or rejection from result filters

The most common output format is JSONL, where each line is one training sample.

## 1. Start the service

### Lightweight local server

```bash
python3 python/scripts/ml_dataset_api.py --port 8787
```

### FastAPI service with docs, auth, rate limits, and persistent jobs

```bash
python3 python/scripts/fastapi_ml_dataset_api.py \
  --port 8790 \
  --job-store /tmp/viospice-ml-jobs.json \
  --api-key your-secret-key \
  --rate-limit 120 \
  --rate-window-seconds 60
```

FastAPI URLs:

- `http://localhost:8790/docs`
- `http://localhost:8790/openapi.json`

## 2. Verify the service

### Lightweight server

```bash
curl http://localhost:8787/api/ml/health
```

### FastAPI server

```bash
curl http://localhost:8790/api/ml/health
```

If auth is enabled, all ML endpoints except health require:

```http
X-API-Key: your-secret-key
```

## 3. Run one simulation sample

Use this when you are validating signal names, measures, labels, or waveform shapes before a large run.

```bash
curl -X POST http://localhost:8790/api/ml/simulate \
  -H 'Content-Type: application/json' \
  -H 'X-API-Key: your-secret-key' \
  -d '{
    "schematic_path": "/home/jnd/qt_projects/viospice/templates/circuits/basics/voltage_divider.sch",
    "analysis": "tran",
    "stop": "10m",
    "step": "10u",
    "signals": ["V(out)"],
    "measures": ["avg V(out)", "max V(out)"],
    "derived_labels": [
      {
        "name": "gain_ratio",
        "expression": {
          "op": "div",
          "left": {"measure": "avg V(out)"},
          "right": {"value": 5.0}
        }
      }
    ],
    "labels": {"family": "divider"},
    "tags": {"split": "debug"}
  }'
```

Use this step to check:

- did the schematic load
- did the requested signals resolve
- are measure names correct
- do derived labels compute as expected
- is waveform size reasonable for training

## 4. Run an explicit batch

Use batch mode when you already have an explicit list of jobs.

```bash
curl -X POST http://localhost:8790/api/ml/batch \
  -H 'Content-Type: application/json' \
  -H 'X-API-Key: your-secret-key' \
  -d '{
    "concurrency": 8,
    "output_path": "/tmp/viospice-datasets/manual_batch.jsonl",
    "jobs": [
      {
        "schematic_path": "/home/jnd/qt_projects/viospice/templates/circuits/basics/voltage_divider.sch",
        "analysis": "tran",
        "stop": "5m",
        "step": "5u",
        "signals": ["V(out)"],
        "measures": ["avg V(out)", "rms V(out)"],
        "labels": {"family": "divider", "ratio": 0.5}
      }
    ]
  }'
```

## 5. Run a sweep to generate many variants

Use sweep mode when you want the service to create large numbers of schematic variants automatically.

```bash
curl -X POST http://localhost:8790/api/ml/sweep \
  -H 'Content-Type: application/json' \
  -H 'X-API-Key: your-secret-key' \
  -d '{
    "template_schematic_path": "/home/jnd/qt_projects/viospice/my_circuits/amp.sch",
    "variant_dir": "/tmp/viospice-variants/amp",
    "output_path": "/tmp/viospice-datasets/amp.jsonl",
    "concurrency": 8,
    "sampling": {
      "mode": "random",
      "sample_count": 1000,
      "seed": 42
    },
    "split_ratios": {
      "train": 0.8,
      "val": 0.1,
      "test": 0.1
    },
    "constraints": [
      {"param": "vin_dc", "op": "<=", "value": "3.3"},
      {"param": "r_feedback", "op": ">", "other_param": "r_input"}
    ],
    "job_template": {
      "analysis": "tran",
      "stop": "10m",
      "step": "10u",
      "signals": ["V(out)"],
      "measures": ["avg V(out)", "rms V(out)", "max V(out)"],
      "derived_labels": [
        {
          "name": "gain_ratio",
          "expression": {
            "op": "div",
            "left": {"measure": "avg V(out)"},
            "right": {"param": "vin_dc"}
          }
        }
      ],
      "result_filters": [
        {
          "measure": "rms V(out)",
          "op": ">=",
          "target": {"value": 0.1}
        }
      ],
      "discard_filtered": true
    },
    "parameters": [
      {
        "name": "r_input",
        "target": {"reference": "R1", "field": "value"},
        "logspace": {"start": 1000, "stop": 100000, "count": 8},
        "engineering_format": "spice",
        "engineering_precision": 4
      },
      {
        "name": "r_feedback",
        "target": {"reference": "R2", "field": "value"},
        "logspace": {"start": 2000, "stop": 200000, "count": 8},
        "engineering_format": "spice",
        "engineering_precision": 4
      },
      {
        "name": "vin_dc",
        "target": {"reference": "V1", "field": "dcVoltage"},
        "linspace": {"start": 1.8, "stop": 5.0, "count": 7},
        "value_format": "{value:.2f}"
      }
    ]
  }'
```

## 6. Use async jobs for long runs

For large batches or sweeps, use async endpoints.

### Submit

```bash
curl -X POST http://localhost:8790/api/ml/jobs/sweep \
  -H 'Content-Type: application/json' \
  -H 'X-API-Key: your-secret-key' \
  -d @sweep_request.json
```

Example response:

```json
{
  "ok": true,
  "job_id": "3fd0f4cb-8b97-4d8f-bd16-91fd6d77d31e",
  "status": "queued",
  "created_at": "2026-04-03T12:00:00+00:00",
  "kind": "sweep"
}
```

### Poll

```bash
curl http://localhost:8790/api/ml/jobs/3fd0f4cb-8b97-4d8f-bd16-91fd6d77d31e \
  -H 'X-API-Key: your-secret-key'
```

Job states:

- `queued`
- `running`
- `completed`
- `failed`

When complete, the job record includes the final result payload.

## 7. Generate a ready-made classification dataset

If you want a real labeled dataset immediately, use the built-in voltage-divider classification endpoint. It runs `vio-cmd netlist-run` across a parameter grid and writes JSONL records with `labels.class_id`.

```bash
curl -X POST http://localhost:8790/api/ml/examples/voltage-divider-classification \
  -H 'Content-Type: application/json' \
  -H 'X-API-Key: your-secret-key' \
  -d @examples/ml_api/voltage_divider_classification_request.json
```

Async version:

```bash
curl -X POST http://localhost:8790/api/ml/jobs/examples/voltage-divider-classification \
  -H 'Content-Type: application/json' \
  -H 'X-API-Key: your-secret-key' \
  -d @examples/ml_api/voltage_divider_classification_request.json
```

The generated labels are:

- `0` for `vout_ratio < 0.35`
- `1` for `0.35 <= vout_ratio < 0.65`
- `2` for `vout_ratio >= 0.65`

## 8. Read the dataset

Each line in the JSONL output is a record. Example Python loader:

```python
import json

records = []
with open("/tmp/viospice-datasets/amp.jsonl", "r", encoding="utf-8") as handle:
    for line in handle:
        if line.strip():
            records.append(json.loads(line))

print("records:", len(records))
print("first labels:", records[0]["labels"])
print("first measures:", records[0]["artifacts"]["measures"])
```

PyTorch-oriented helper:

```bash
python3 examples/ml_api/pytorch_dataset.py
```

That helper can:

- load JSONL records
- skip rejected samples
- extract stat-based features
- extract parameter features
- return tensors through `VioSpiceJsonlDataset` when `torch` is installed

End-to-end training example:

```bash
python3 examples/ml_api/train_regressor.py
```

That script:

- loads JSONL into `VioSpiceJsonlDataset`
- splits the data into train and validation sets
- trains a small MLP regressor
- prints train and validation loss per epoch

Classification example:

```bash
python3 examples/ml_api/generate_voltage_divider_classification_dataset.py
python3 examples/ml_api/train_classifier.py
```

The classification example generates a real voltage-divider dataset with `vio-cmd netlist-run`, assigns `labels.class_id` from the simulated `vout_ratio`, and then trains on that JSONL.

Notebook version:

```bash
jupyter notebook examples/ml_api/train_regressor_notebook.ipynb
```

Typical fields to use in training:

- inputs:
  - `artifacts.waveforms`
  - `artifacts.stats`
  - `metadata.sweep_values`
- targets:
  - `labels`
- filters:
  - `accepted`
  - `result_filters`

## 9. Recommended workflow

1. Start with `POST /api/ml/simulate` on one schematic.
2. Confirm signal names, measures, derived labels, and waveform sizes.
3. Build a sweep with parameter generators and constraints.
4. Add `result_filters` to keep only useful samples.
5. Use async sweep submission for long runs.
6. Train from JSONL records using `labels` as targets and waveforms or stats as features.

## 10. Practical tips

- Prefer `max_points` to control waveform size before training.
- Use `sampling.mode=random` when exhaustive cartesian expansion is too large.
- Use `engineering_format: "spice"` for schematic fields like resistor or capacitor values.
- Use `constraints` to reject invalid circuit configurations before simulation.
- Use `discard_filtered: true` when you only want accepted samples in the final JSONL.
- Persist async jobs with `--job-store` if runs may survive server restarts.

## 11. Where to look next

- API reference: [ML_DATASET_API.md](/home/jnd/qt_projects/viospice/docs/ML_DATASET_API.md)
- ready-to-run examples: [examples/ml_api/README.md](/home/jnd/qt_projects/viospice/examples/ml_api/README.md)
- PyTorch loader: [examples/ml_api/pytorch_dataset.py](/home/jnd/qt_projects/viospice/examples/ml_api/pytorch_dataset.py)
- training notebook: [examples/ml_api/train_regressor_notebook.ipynb](/home/jnd/qt_projects/viospice/examples/ml_api/train_regressor_notebook.ipynb)
- FastAPI docs: `/docs`
- OpenAPI schema: `/openapi.json`
