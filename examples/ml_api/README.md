# ML API Examples

This folder contains runnable request templates and a minimal Python client for the VioSpice ML service.

## Files

- `simulate_request.json`: one synchronous simulation sample
- `batch_request.json`: one synchronous batch request
- `sweep_request.json`: one sweep request for dataset generation
- `python_client.py`: submits an async sweep job and polls until completion

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

## Python client

Edit `BASE_URL`, `API_KEY`, and the file paths inside `python_client.py`, then run:

```bash
python3 examples/ml_api/python_client.py
```
