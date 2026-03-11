# Test Layout

Structured test directories:

- `tests/unit/`: fast, isolated unit tests.
- `tests/integration/`: cross-module behavior tests.
- `tests/regression/`: fixtures and regression scenarios.
- `tests/archive/ad_hoc/`: migrated legacy root test artifacts.

Notes:

- New tests must not be placed in project root.
- Legacy ad-hoc files are archived for reference and can be migrated incrementally.
