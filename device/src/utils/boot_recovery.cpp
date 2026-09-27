#include "utils/boot_recovery.h"

#include <Preferences.h>

namespace BootRecovery {

bool checkAndMarkPessimistic() {
  Preferences prefs;
  prefs.begin("moana", false);
  bool prevBootClean = prefs.getBool("lfs_ok", true);
  prefs.putBool("lfs_ok", false);  // pessimistic: assume crash until first LFS write succeeds
  prefs.end();

  return !prevBootClean;
}

void markClean() {
  Preferences prefs;
  prefs.begin("moana", false);
  prefs.putBool("lfs_ok", true);
  prefs.end();
}

}  // namespace BootRecovery
