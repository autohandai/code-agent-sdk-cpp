# Startup Performance

The `autohand_sdk_tests` executable guards wrapper-controlled startup with a
deterministic in-process JSON-RPC fixture. Each metric uses five warmup
iterations and 50 measured samples, reports median and p95 latency, and fails
if any p95 reaches 50 ms.

- `publicImportMs`: the first public `autohand::initialize()` call timed inside
  a fresh C++ process, excluding C++ runtime process boot. This idempotent hook
  performs the library's eager runtime initialization.
- `sdkStartReturnMs`: elapsed time for the public `AutohandSdk::start` API,
  including its readiness `getState` request.
- `fixtureSpawnToFirstRpcMs`: elapsed time to spawn the deterministic fixture
  and complete its first successful `getState` request.

Baseline captured on 2026-07-20:

| Metric | Median | p95 | Budget |
| --- | ---: | ---: | ---: |
| `publicImportMs` | 0.012083 ms | 0.014959 ms | < 50 ms |
| `sdkStartReturnMs` | 3.140166 ms | 3.888250 ms | < 50 ms |
| `fixtureSpawnToFirstRpcMs` | 3.450083 ms | 4.102292 ms | < 50 ms |

Run the gate directly:

```bash
cmake --build build --target autohand_sdk_tests
./build/autohand_sdk_tests
```

The test emits one machine-readable JSON object with top-level `language`,
`budgetMs`, `metrics`, and `passed` fields. Each metric contains `samples`,
`medianMs`, `p95Ms`, `maxMs`, and `passed`. The fixed protocol is five warmups
and 50 measured samples.

This gate isolates wrapper overhead. Real CLI startup may additionally perform
provider authentication, network access, model loading, and other
environment-specific readiness work; those live measurements are deliberately
reported separately from the deterministic 50 ms gate.
