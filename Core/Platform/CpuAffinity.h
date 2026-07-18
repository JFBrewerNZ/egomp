#pragma once

// Pin this client to a single CPU core.
//
// Fable TLC's engine has timing/threading assumptions that break across
// multiple cores: broken animation framerate, missing sounds/voices, a
// disproportionately huge hero model, and assorted timing glitches (the
// long-standing community fix is `start /affinity <mask>`). Running two EgoMP
// clients at once amplifies the scheduling sensitivity, so each client is
// pinned to its OWN single core (by client slot), avoiding core 0 (the busiest)
// and avoiding contention between the two instances.
//
// Gated on [performance] cpu_affinity in EgoMP.ini (default on). Runs at attach,
// before the game's threads spin up.
namespace CpuAffinity
{
    void Install();
}
