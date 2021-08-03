#include <windows.h>

// Index value of power actions. Can't find in official doc.
enum { INDEX_DO_NOTHING = 0, INDEX_SLEEP, INDEX_HIBERNATE, INDEX_SHUT_DOWN };
// Read the power action of lid closing if on battery.
// Return value is the error code(ERROR_SUCCESS etc.).
DWORD readLidCloseActionIndexDC(DWORD *index);
// Read the power action of lid closing if AC plugged in.
// Return value is the error code(ERROR_SUCCESS etc.).
DWORD readLidCloseActionIndexAC(DWORD *index);
// Set the power action of lid closing if on battery.
// Return value is the error code(ERROR_SUCCESS etc.).
DWORD writeLidCloseActionIndexDC(DWORD index);
// Set the power action of lid closing if AC plugged in.
// Return value is the error code(ERR_SUCCESS etc.).
DWORD writeLidCloseActionIndexAC(DWORD index);