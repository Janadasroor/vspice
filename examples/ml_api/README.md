# ML API Examples

This folder contains runnable request templates and a minimal Python client for the VioSpice ML service.

## Files

- `simulate_request.json`: one synchronous simulation sample
- `batch_request.json`: one synchronous batch request
- `sweep_request.json`: one sweep request for dataset generation
- `voltage_divider_classification_request.json`: request for a real classification dataset generated through the API
- `python_client.py`: submits an async sweep job and polls until completion
- `pytorch_dataset.py`: loads generated JSONL into a simple PyTorch-compatible dataset
- `train_regressor.py`: end-to-end PyTorch regression example over generated JSONL
- `train_regressor_notebook.ipynb`: notebook version of the regression example
- `generate_voltage_divider_classification_dataset.py`: generates a real circuit-backed classification dataset with `vio-cmd netlist-run`
- `train_classifier.py`: end-to-end PyTorch classification example over generated JSONL

## cURL examples

```bash
curl -X POST http://localhost:8790/api/ml/simulate \
  -H 'Content-Type: application/json' \
  -H 'X-API-Key: your-secret-key' \
  -d @examples/ml_api/simulate_request.json
```

```bash
curl -X POST http://localhost:8790/api/ml/batch \
  -H 'Content-Type: application/json' \
  -H 'X-API-Key: your-secret-key' \
  -d @examples/ml_api/batch_request.json
```

```bash
curl -X POST http://localhost:8790/api/ml/jobs/sweep \
  -H 'Content-Type: application/json' \
  -H 'X-API-Key: your-secret-key' \
  -d @examples/ml_api/sweep_request.json
```

```bash
curl -X POST http://localhost:8790/api/ml/examples/voltage-divider-classification \
  -H 'Content-Type: application/json' \
  -H 'X-API-Key: your-secret-key' \
  -d @examples/ml_api/voltage_divider_classification_request.json
```

## Python client

Edit `BASE_URL`, `API_KEY`, and the file paths inside `python_client.py`, then run:

```bash
python3 examples/ml_api/python_client.py
```

## PyTorch dataset helper

After generating JSONL:

```bash
python3 examples/ml_api/pytorch_dataset.py
```

If `torch` is installed, you can also import `VioSpiceJsonlDataset` directly in your training code.

## Training example

After generating a JSONL dataset:

```bash
python3 examples/ml_api/train_regressor.py
```

Edit the constants at the top of the script to point at your dataset and choose your feature and label names.

For classification:

```bash
python3 examples/ml_api/generate_voltage_divider_classification_dataset.py
python3 examples/ml_api/train_classifier.py
```

The classifier example uses a generated voltage-divider dataset at `/tmp/viospice-datasets/voltage_divider_classifier.jsonl`. If that file does not exist, `train_classifier.py` generates it automatically with real simulator runs.

## Notebook

Open:

```bash
jupyter notebook examples/ml_api/train_regressor_notebook.ipynb
```

or

```bash
jupyter lab examples/ml_api/train_regressor_notebook.ipynb
```
