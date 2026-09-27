#pragma once

namespace BootRecovery {

// Returns true if previous boot crashed during LFS write.
// Sets pessimistic flag (assumes crash until markClean is called).
bool checkAndMarkPessimistic();

void markClean();

}  // namespace BootRecovery
