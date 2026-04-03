# ML Dataset API

This API exposes VioSpice simulation runs as training-ready dataset records so ML engineers can run thousands of circuit jobs and collect rich artifacts per sample.

## What each sample contains

- source schematic path and simulation configuration
- generated schematic netlist in JSON form
- simulator-native result JSON from `vio-cmd simulate --json`
- optional decimated raw waveforms extracted from the returned signals
- optional signal statistics computed by the API
- optional measurement expressions computed by the API
- user-provided labels, tags, and metadata

## Start the server

```bash
python3 python/scripts/ml_dataset_api.py --port 8787
```

Optional:

```bash
python3 python/scripts/ml_dataset_api.py --port 8787 --cli-path ./build/vio-cmd
```

## FastAPI Packaging

For OpenAPI docs and ASGI deployment:

```bash
python3 python/scripts/fastapi_ml_dataset_api.py --port 8790
```

Then use:

- `http://localhost:8790/docs`
- `http://localhost:8790/openapi.json`

Install the additional API dependencies from [requirements.txt](/home/jnd/qt_projects/viospice/python/requirements.txt) first if they are not already present.

## Endpoints

### `GET /api/ml/health`

Returns service status and the resolved `vio-cmd` path.

### `POST /api/ml/simulate`

Runs one job and returns one rich sample.

Example request:

```json
{
  "schematic_path": "/home/jnd/qt_projects/viospice/templates/circuits/basics/voltage_divider.sch",
  "analysis": "tran",
  "stop": "10m",
  "step": "10u",
  "signals": ["V(out)", "V(in)"],
  "measures": ["avg V(out)", "max V(out)"],
  "max_points": 2000,
  "labels": {
    "target_gain": 0.5
  },
  "tags": {
    "family": "divider",
    "split": "train"
  }
}
```

### `POST /api/ml/batch`

Runs many jobs concurrently. For large dataset generation, write the results directly to JSONL.

Example request:

```json
{
  "concurrency": 8,
  "output_path": "/tmp/viospice-datasets/dividers.jsonl",
  "jobs": [
    {
      "schematic_path": "/home/jnd/qt_projects/viospice/templates/circuits/basics/voltage_divider.sch",
      "analysis": "tran",
      "stop": "5m",
      "step": "5u",
      "signals": ["V(out)"],
      "measures": ["avg V(out)", "rms V(out)"],
      "result_filters": [
        {
          "measure": "rms V(out)",
          "op": ">=",
          "target": {"value": 0.1}
        }
      ],
      "labels": {
        "ratio": 0.5
      }
    },
    {
      "schematic_path": "/home/jnd/qt_projects/viospice/templates/circuits/filters/rc_low_pass.sch",
      "analysis": "ac",
      "signals": ["V(out)"],
      "labels": {
        "family": "low_pass"
      }
    }
  ]
}
```

Each line in the output JSONL file is a self-contained training record.

### `POST /api/ml/sweep`

Expands a single template schematic into many variant schematics using a cartesian product of parameter values, then runs the generated jobs as a batch.

Example request:

```json
{
  "template_schematic_path": "/home/jnd/qt_projects/viospice/my_circuits/amp.sch",
  "variant_dir": "/tmp/viospice-variants/amp-sweep",
  "output_path": "/tmp/viospice-datasets/amp-sweep.jsonl",
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
    {
      "param": "vin_dc",
      "op": "<=",
      "value": "3.3"
    },
    {
      "param": "r_feedback",
      "op": ">",
      "other_param": "r_input"
    }
  ],
  "job_template": {
    "analysis": "tran",
    "stop": "10m",
    "step": "10u",
    "signals": ["V(out)"],
    "measures": ["avg V(out)", "rms V(out)"],
    "derived_labels": [
      {
        "name": "gain_ratio",
        "expression": {
          "op": "div",
          "left": {"measure": "avg V(out)"},
          "right": {"param": "vin_dc"}
        }
      }
    ]
  },
  "parameters": [
    {
      "name": "r_gain",
      "target": {
        "reference": "R1",
        "field": "value"
      },
      "logspace": {
        "start": 1000,
        "stop": 10000,
        "count": 4
      },
      "engineering_format": "spice",
      "engineering_precision": 4
    },
    {
      "name": "vin_dc",
      "target": {
        "reference": "V1",
        "field": "dcVoltage"
      },
      "linspace": {
        "start": 1.8,
        "stop": 5.0,
        "count": 5
      },
      "value_format": "{value:.2f}"
    }
  ]
}
```

Supported target forms:

- item selector plus field:
  - `{"reference":"R1","field":"value"}`
  - `{"reference":"V1","field":"sineFrequency"}`
- direct JSON path:
  - `{"json_path":"items[0].value"}`

Supported parameter value definitions:

- explicit list:
  - `{"values":["1k","2k","5k"]}`
- linear spacing:
  - `{"linspace":{"start":1.8,"stop":5.0,"count":5},"value_format":"{value:.2f}"}`
- logarithmic spacing:
  - `{"logspace":{"start":1e3,"stop":1e6,"count":4},"value_format":"{value:.3e}"}`
- integer range:
  - `{"range":{"start":1,"stop":8,"step":1,"inclusive":true}}`

Engineering formatting:

- `{"engineering_format":"spice"}` emits suffixes like `1k`, `10u`, `4.7n`
- `engineering_precision` controls significant digits for the generated SPICE string
- `value_format` still works for normal Python-style numeric formatting when engineering suffixes are not needed

Each generated dataset record includes `metadata.sweep_values` and points to the generated variant schematic path used for that sample.

Sampling modes:

- `{"mode":"exhaustive"}`: generate every combination
- `{"mode":"random","sample_count":1000,"seed":42}`: generate a deterministic random subset

Split handling:

- `split_ratios` assigns `metadata.split` and `tags.split` for each generated sample
- assignment is deterministic when a sampling `seed` is provided
- default split ratios are `train=0.8`, `val=0.1`, `test=0.1`

Constraint handling:

- constraints are applied before sampling and before split assignment
- parameter-to-constant example:
  - `{"param":"vin_dc","op":"<=","value":"3.3"}`
- parameter-to-parameter example:
  - `{"param":"r_feedback","op":">","other_param":"r_input"}`
- supported operators:
  - `==`, `!=`, `>`, `<`, `>=`, `<=`, `in`, `not_in`

Derived labels:

- derived labels are computed after measures and stats are available
- they are merged into the record `labels` object
- supported operand sources:
  - `{"param":"vin_dc"}`
  - `{"measure":"avg V(out)"}`
  - `{"stat":{"signal":"V(out)","field":"rms"}}`
  - `{"label":"existing_label_name"}`
  - `{"value":2}`
- supported expression operators:
  - `add`, `sub`, `mul`, `div`, `pow`, `min`, `max`, `abs`

Result filters:

- result filters are evaluated after measures, stats, and derived labels are computed
- each record gets:
  - `accepted`: `true` or `false`
  - `result_filters`: rule evaluation summary
- with `discard_filtered: true`, rejected records are omitted from JSONL batch output
- supported sources on the left side:
  - `measure`, `stat`, `label`, `param`, `value`
- example:
  - `{"measure":"rms V(out)","op":">=","target":{"value":0.1}}`

## Notes

- `analysis` supports `op`, `tran`, and `ac`
- `include_netlist`, `include_raw`, and `include_stats` default to `true`
- `measures` currently supports `avg`, `max`, `min`, `rms`, `pp`, and `value ... at <time>`
- `max_points` lets you decimate waveforms before exporting them into the dataset
- `range` lets you crop the exported waveform window
- `compat` enables the CLI compatibility layer for imported SPICE decks when needed
- `sweep` generation currently materializes variant `.sch` files by patching JSON fields in the template schematic
- `sampling.mode=random` samples combinations without replacement by default
