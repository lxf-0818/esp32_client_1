# rollBack.cpp

## Purpose
Contains rollback/delete helper wrappers for backend cleanup operations.

## Current Implementation
- `deleteRow(phpScript)` calls `performHttpGet(phpScript.c_str())`
- currently returns `1` unconditionally

## Notes
- This module is minimal and relies on HTTP helper in main runtime.
- Return handling is currently optimistic; consider propagating actual HTTP success/failure for stronger recovery behavior.
