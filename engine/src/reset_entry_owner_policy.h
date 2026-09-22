#pragma once
#include "lease_wait_diagnostic.h"
// Which lease owner may publish an install request for the single
// process-wide command-list Reset entry.
//
// The pool hooks exactly one Reset target and refuses every Begin whose list
// resolves to a different one, so whoever requests first owns NR's fate for
// the rest of the process. YanYun 2026-09-09/10 proved the failure mode: the
// ReShade presentation grade armed 18 seconds before the NGX NR input (a
// zero-pixel signature effect was enough to start ReShade's effect pass), took
// the slot, and NR recorded 0 frames for the whole session while `bypass` grew
// at exactly the NR frame rate. The same build on the entry whose ReShade had
// no effect search path recorded 6000+ NR frames.
//
// Reserving the slot for the game's own render lists reproduces that verified
// configuration. It is not a second hook: exactly one entry is still installed,
// and no live hook is ever replaced or detached.
namespace resetentryowner033 {
inline bool MayClaim(leasewait033::Owner owner,bool nrEnabled) {
    // With NR off there is no NR list to protect, so the presentation grade
    // keeps the behaviour it has today.
    return !nrEnabled || owner!=leasewait033::Owner::GradePresent;
}
}
