#!/usr/bin/env python3
import random
import subprocess
from pathlib import Path

from pytorch_dataset import load_jsonl, extract_feature_vector


try:
    import torch
    from torch import nn
    from torch.utils.data import DataLoader, TensorDataset, random_split
except ImportError as exc:  # pragma: no cover
    raise SystemExit("torch is not installed. Install torch to run this example.") from exc


DATASET_PATH = "/tmp/viospice-datasets/voltage_divider_classifier.jsonl"
GENERATOR_PATH = Path(__file__).with_name("generate_voltage_divider_classification_dataset.py")
CLASS_LABEL = "class_id"
STAT_FEATURES = [("ALL", "avg"), ("ALL", "max"), ("ALL", "rms")]
PARAM_FEATURES = ["vin_dc", "r1_ohms", "r2_ohms"]
SEED = 42
EPOCHS = 20
BATCH_SIZE = 32
LR = 1e-3


class Classifier(nn.Module):
    def __init__(self, input_dim: int, num_classes: int):
        super().__init__()
        self.net = nn.Sequential(
            nn.Linear(input_dim, 32),
            nn.ReLU(),
            nn.Linear(32, 32),
            nn.ReLU(),
            nn.Linear(32, num_classes),
        )

    def forward(self, x):
        return self.net(x)


def ensure_dataset(dataset_path: Path) -> None:
    if dataset_path.exists():
        return
    subprocess.run(["python3", str(GENERATOR_PATH)], check=True)


def build_tensor_dataset(dataset_path: str):
    records = load_jsonl(dataset_path, accepted_only=True)
    if not records:
        raise SystemExit("no accepted records found")

    features = []
    labels = []
    for record in records:
        label = record.get("labels", {}).get(CLASS_LABEL)
        if label is None:
            continue
        features.append(
            extract_feature_vector(
                record,
                stat_features=STAT_FEATURES,
                param_features=PARAM_FEATURES,
            )
        )
        labels.append(int(label))

    if not features:
        raise SystemExit(f"no records contained label '{CLASS_LABEL}'")

    x = torch.tensor(features, dtype=torch.float32)
    y = torch.tensor(labels, dtype=torch.long)
    return TensorDataset(x, y)


def split_dataset(dataset):
    total = len(dataset)
    train_size = max(1, int(total * 0.8))
    val_size = max(1, total - train_size)
    if train_size + val_size > total:
        train_size = total - 1
        val_size = 1
    generator = torch.Generator().manual_seed(SEED)
    return random_split(dataset, [train_size, val_size], generator=generator)


def evaluate(model, loader, loss_fn, device):
    model.eval()
    losses = []
    correct = 0
    total = 0
    with torch.no_grad():
        for x, y in loader:
            x = x.to(device)
            y = y.to(device)
            logits = model(x)
            loss = loss_fn(logits, y)
            losses.append(float(loss.item()))
            preds = logits.argmax(dim=1)
            correct += int((preds == y).sum().item())
            total += int(y.numel())
    return {
        "loss": sum(losses) / max(1, len(losses)),
        "accuracy": correct / max(1, total),
    }


def main():
    random.seed(SEED)
    torch.manual_seed(SEED)

    dataset_path = Path(DATASET_PATH)
    ensure_dataset(dataset_path)
    if not dataset_path.exists():
        raise SystemExit(f"dataset not found: {dataset_path}")

    dataset = build_tensor_dataset(str(dataset_path))
    if len(dataset) < 2:
        raise SystemExit("dataset is too small for a train/validation split")

    train_ds, val_ds = split_dataset(dataset)
    train_loader = DataLoader(train_ds, batch_size=BATCH_SIZE, shuffle=True)
    val_loader = DataLoader(val_ds, batch_size=BATCH_SIZE, shuffle=False)

    sample_x, sample_y = dataset[0]
    num_classes = int(max(item[1].item() for item in dataset)) + 1
    model = Classifier(sample_x.shape[0], num_classes)
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    model.to(device)

    optimizer = torch.optim.Adam(model.parameters(), lr=LR)
    loss_fn = nn.CrossEntropyLoss()

    for epoch in range(1, EPOCHS + 1):
        model.train()
        epoch_losses = []
        for x, y in train_loader:
            x = x.to(device)
            y = y.to(device)
            optimizer.zero_grad()
            logits = model(x)
            loss = loss_fn(logits, y)
            loss.backward()
            optimizer.step()
            epoch_losses.append(float(loss.item()))

        train_loss = sum(epoch_losses) / max(1, len(epoch_losses))
        metrics = evaluate(model, val_loader, loss_fn, device)
        print(
            f"epoch={epoch:02d} "
            f"train_loss={train_loss:.6f} "
            f"val_loss={metrics['loss']:.6f} "
            f"val_acc={metrics['accuracy']:.4f}"
        )

    model.eval()
    with torch.no_grad():
        pred = model(sample_x.unsqueeze(0).to(device)).cpu().argmax(dim=1).item()
    print("sample target:", int(sample_y.item()))
    print("sample prediction:", pred)


if __name__ == "__main__":
    main()
