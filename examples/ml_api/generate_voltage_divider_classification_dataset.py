#!/usr/bin/env python3
import json
import math
import subprocess
from pathlib import Path
from typing import Dict, List


REPO_ROOT = Path(__file__).resolve().parents[2]
VIO_CMD = REPO_ROOT / "build" / "vio-cmd"
OUTPUT_PATH = Path("/tmp/viospice-datasets/voltage_divider_classifier.jsonl")
NETLIST_DIR = Path("/tmp/viospice-netlists/voltage_divider_classifier")
VIN_VALUES = [1.8, 3.3, 5.0, 12.0]
R1_VALUES = [470, 1000, 2200, 4700, 10000]
R2_VALUES = [470, 1000, 2200, 4700, 10000]


def _spice_resistance(value_ohms: int) -> str:
    if value_ohms >= 1000:
        scaled = value_ohms / 1000.0
        if math.isclose(scaled, round(scaled), rel_tol=0.0, abs_tol=1e-12):
            return f"{int(round(scaled))}k"
        return f"{scaled:g}k"
    return str(value_ohms)


def _run_netlist(netlist_path: Path) -> Dict[str, object]:
    result = subprocess.run(
        [str(VIO_CMD), "netlist-run", str(netlist_path), "--json", "--stats"],
        cwd=str(REPO_ROOT),
        capture_output=True,
        text=True,
        check=True,
    )
    return json.loads(result.stdout)


def _find_stat(stats: List[Dict[str, object]], name: str) -> Dict[str, object]:
    for item in stats:
        if str(item.get("name", "")).upper() == name.upper():
            return item
    raise KeyError(f"stat not found: {name}")


def _classify_ratio(ratio: float) -> int:
    if ratio < 0.35:
        return 0
    if ratio < 0.65:
        return 1
    return 2


def _split_name(index: int, total: int) -> str:
    train_cutoff = int(total * 0.8)
    val_cutoff = int(total * 0.9)
    if index < train_cutoff:
        return "train"
    if index < val_cutoff:
        return "val"
    return "test"


def build_record(index: int, total: int, vin_dc: float, r1_ohms: int, r2_ohms: int) -> Dict[str, object]:
    netlist_path = NETLIST_DIR / f"divider_{index:05d}.cir"
    netlist_path.write_text(
        "\n".join(
            [
                "* generated voltage divider classifier sample",
                f"V1 in 0 DC {vin_dc}",
                f"R1 in out {_spice_resistance(r1_ohms)}",
                f"R2 out 0 {_spice_resistance(r2_ohms)}",
                ".op",
                ".end",
                "",
            ]
        ),
        encoding="utf-8",
    )

    simulation = _run_netlist(netlist_path)
    stats = list(simulation.get("stats") or [])
    out_stat = _find_stat(stats, "V(OUT)")
    current_stat = _find_stat(stats, "I(V1)")
    vout = float(out_stat["avg"])
    ratio = vout / vin_dc if vin_dc else 0.0
    class_id = _classify_ratio(ratio)
    split = _split_name(index, total)

    return {
        "job_id": f"divider-{index:05d}",
        "ok": bool(simulation.get("ok", False)),
        "created_at": None,
        "duration_seconds": 0.0,
        "source": {
            "kind": "netlist-run",
            "netlist_path": str(netlist_path),
            "analysis": "op",
            "signals": ["V(OUT)", "I(V1)"],
        },
        "labels": {
            "class_id": class_id,
            "vout_ratio": ratio,
            "vout_avg": vout,
        },
        "tags": {
            "family": "voltage_divider",
            "split": split,
            "task": "classification",
        },
        "metadata": {
            "split": split,
            "sweep_values": {
                "vin_dc": vin_dc,
                "r1_ohms": r1_ohms,
                "r2_ohms": r2_ohms,
            },
            "class_rule": {
                "class_0": "vout_ratio < 0.35",
                "class_1": "0.35 <= vout_ratio < 0.65",
                "class_2": "vout_ratio >= 0.65",
            },
        },
        "accepted": True,
        "result_filters": {"passed": True, "rule_count": 0, "evaluations": []},
        "discard_filtered": False,
        "artifacts": {
            "netlist": netlist_path.read_text(encoding="utf-8"),
            "simulation": simulation,
            "waveforms": [],
            "stats": [
                {
                    "name": "ALL",
                    "avg": vout,
                    "max": vout,
                    "min": vout,
                    "rms": vout,
                },
                {
                    "name": "I(V1)",
                    "avg": float(current_stat["avg"]),
                    "max": float(current_stat["max"]),
                    "min": float(current_stat["min"]),
                    "rms": float(current_stat["rms"]),
                },
            ],
            "measures": [
                {"expr": "avg V(OUT)", "value": vout},
                {"expr": "ratio V(OUT)/VIN", "value": ratio},
            ],
        },
    }


def main() -> None:
    if not VIO_CMD.exists():
        raise SystemExit(f"vio-cmd not found: {VIO_CMD}")

    OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    NETLIST_DIR.mkdir(parents=True, exist_ok=True)

    combos = [(vin, r1, r2) for vin in VIN_VALUES for r1 in R1_VALUES for r2 in R2_VALUES]
    records = [build_record(index, len(combos), vin, r1, r2) for index, (vin, r1, r2) in enumerate(combos)]

    with open(OUTPUT_PATH, "w", encoding="utf-8") as handle:
        for record in records:
            handle.write(json.dumps(record, ensure_ascii=True) + "\n")

    class_counts = {0: 0, 1: 0, 2: 0}
    for record in records:
        class_counts[int(record["labels"]["class_id"])] += 1

    print(f"wrote records: {len(records)}")
    print(f"output path: {OUTPUT_PATH}")
    print(f"class counts: {class_counts}")


if __name__ == "__main__":
    main()
