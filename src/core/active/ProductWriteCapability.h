#pragma once

namespace modbuslens::core {

// ---------------------------------------------------------------------------
// M10-D3: the PRODUCT-level 0x06 write capability.
//
// Two capability levels are deliberately kept apart (M10-D Phase 1 §T1):
//
//   A. `activeFunctionSupported(0x06)` — core/serial: the protocol/session
//      layer can correctly run the 0x06 request/response lifecycle. It says
//      NOTHING about the product and controls no UI.
//
//   B. `kProductWrite06Supported` (this constant) — the PRODUCT end-to-end
//      owns all four pieces: the 0x06 encoder, the protocol/session response
//      support, the Controller's atomic confirm+dispatch operation, and the
//      evidence/outcome integration.
//
// It is a STRUCTURAL fact of the current build, NOT a runtime availability
// flag. It must therefore NOT be derived from — and must never change with —
// connection state, busy state, draft validity or the current data source.
// Those belong to the ACTION enabled state, which is a different question:
// binding visibility to runtime state would unload the whole write UI (and
// the user's draft) the moment the port disconnects.
//
// Deliberately a compile-time constant rather than something that flips to
// true once a test passes: a capability that a test run can switch on would be
// a statement about the test, not about the product. `docs/tasks/T022` §W
// records the acceptance chain that justifies the value; `tests/test_write_dispatch.cpp`
// cross-checks it against the real runtime predicates so it can never drift
// away from the code it claims to describe.
//
// Capability ready != presentation rollout (M10-D Phase 1 §T1): the production
// Write UI stays hidden until M10-D4 no matter what this constant says.
// ---------------------------------------------------------------------------
inline constexpr bool kProductWrite06Supported = true;

} // namespace modbuslens::core
