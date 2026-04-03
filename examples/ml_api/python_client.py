#!/usr/bin/env python3
import json
import time
import urllib.error
import urllib.request


BASE_URL = "http://localhost:8790"
API_KEY = "your-secret-key"


def request_json(method: str, path: str, payload=None):
    data = None
    headers = {"Content-Type": "application/json"}
    if API_KEY:
        headers["X-API-Key"] = API_KEY
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(f"{BASE_URL}{path}", data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace")
        raise SystemExit(f"HTTP {exc.code}: {body}") from exc


def poll_job(job_id: str, interval_seconds: float = 2.0):
    while True:
        job = request_json("GET", f"/api/ml/jobs/{job_id}")
        status = job.get("status")
        print(f"job={job_id} status={status}")
        if status in {"completed", "failed"}:
            return job
        time.sleep(interval_seconds)


def main():
    health = request_json("GET", "/api/ml/health")
    print("health:", json.dumps(health, indent=2))

    payload = {
        "template_schematic_path": "/home/jnd/qt_projects/viospice/templates/circuits/basics/voltage_divider.sch",
        "output_path": "/tmp/viospice-datasets/voltage_divider.jsonl",
        "concurrency": 4,
        "sampling": {"mode": "random", "sample_count": 8, "seed": 42},
        "split_ratios": {"train": 0.75, "val": 0.125, "test": 0.125},
        "job_template": {
            "analysis": "tran",
            "stop": "2m",
            "step": "10u",
            "signals": ["ALL"],
            "measures": ["avg ALL", "max ALL"],
            "derived_labels": [
                {
                    "name": "avg_to_vin_ratio",
                    "expression": {
                        "op": "div",
                        "left": {"measure": "avg ALL"},
                        "right": {"param": "vin_dc"},
                    },
                }
            ],
        },
        "parameters": [
            {
                "name": "vin_dc",
                "target": {"json_path": "items[0].dcVoltage"},
                "linspace": {"start": 1.8, "stop": 5.0, "count": 8},
                "value_format": "{value:.2f}",
            }
        ],
    }

    accepted = request_json("POST", "/api/ml/jobs/sweep", payload)
    print("accepted:", json.dumps(accepted, indent=2))

    final_job = poll_job(accepted["job_id"])
    print("final:", json.dumps(final_job, indent=2))


if __name__ == "__main__":
    main()
