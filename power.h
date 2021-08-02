#include <windows.h>

// Index value of power actions. Can't find in official doc.
enum { INDEX_DO_NOTHING = 0, INDEX_SLEEP, INDEX_HIBERNATE, INDEX_SHUT_DOWN };
// Read the power action of lid closing if on battery.
// Return value is the error code(ERROR_SUCCESS etc.).
DWORD readLidCloseActionIndex_Battery(DWORD *index);
// Read the power action of lid closing if AC plugged in.
// Return value is the error code(ERROR_SUCCESS etc.).
DWORD readLidCloseActionIndex_PluggedIn(DWORD *index);
// Set the power action of lid closing if on battery.
// Return value is the error code(ERROR_SUCCESS etc.).
DWORD writeLidCloseActionIndex_Battery(DWORD index);
// Set the power action of lid closing if AC plugged in.
// Return value is the error code(ERR_SUCCESS etc.).
DWORD writeLidCloseActionIndex_PluggedIn(DWORD index);