#include <M5Unified.h>

namespace Utils {

// Set the process-wide display timezone (NZ) once, at startup. Must be called
// before the logger or any getLocalTime() consumer runs (R10).
void initTimezone();

void monitoringTask(void* pvParameters);
bool syncTimeWithNTP();
m5::rtc_time_t getLocalTime();

}  // namespace Utils
