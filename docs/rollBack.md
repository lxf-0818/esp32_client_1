# rollBack.cpp

## Purpose
Contains rollback and cleanup helper wrappers for backend recovery paths.

## Current implementation
- deleteRow(phpScript) delegates to performHttpGet() for the supplied PHP URL.
- The helper currently returns 1 unconditionally, so the caller should treat it as a best-effort cleanup trigger rather than a proven success signal.

## Notes
- This module is intentionally small and relies on the shared HTTP helper from the main runtime.
- The current code path is used by recovery logic when the backend needs a stale row or MAC entry removed.
