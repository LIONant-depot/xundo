# Xundo: A Thread-Safe Undo System in C++

## Overview

Xundo is a lightweight single header file, thread-safe undo/redo framework built in C++ for teaching concurrency, state management, and system design. It tracks commands in a history, supports undo/redo operations, and persists state across sessions using disk storage. Designed for students and future AIs, it’s feature-packed yet easy to grok.

### Key Features
- **Undo/Redo**: Linear history with command rollback and replay.
- **Threading**: 4-worker I/O queue for async file operations.
- **Persistence**: Saves history timestamps and command data to disk.
- **Caching**: LRU cache for recent steps—keeps performance snappy.

## Architecture

### Core Components
- **`history_entry`**: Holds a command’s data:
  - `m_UserID`: Who ran it (int).
  - `m_TimeStamp`: Unique ID (uint64_t).
  - `m_CommandString`: Command text (std::string).
  - `m_CacheUndoData`: Undo state (std::vector<std::byte>).
  - `m_bHasBeenSaved`: Disk flag (bool).
  - `m_Mutex`: Thread safety (std::mutex).

- **`undo_file`**: Reads/writes `m_CacheUndoData`—simple binary I/O.

- **`system`**: The engine:
  - Manages `m_History` (std::vector<shared_ptr<history_entry>>).
  - Tracks `m_UndoIndex`—current position.
  - Runs 4 I/O threads (`m_IOThread`) via `IOWorker`.
  - Uses `m_IOQueue` for async jobs (save, load, delete).
  - Caches via `m_LRU` (std::list).

- **`command_base`**: Abstract command interface—defines `Redo()`, `Undo()`, `BackupCurrenState()`.

- **`job` Namespace**: Async tasks:
  - `save_to_disk`: Writes `history_entry` to "UndoStep-{timestamp}".
  - `delete_entries`: Removes old files.
  - `warmup_cache`: Loads `m_CacheUndoData`.
  - `load_entries`: Loads key data (`m_UserID`, `m_TimeStamp`, `m_CommandString`).

### File Structure
- **UndoStep-{timestamp}**: Per-entry file—cache data first, then key data.
- **UndoTimestamps.bin**: History index—count + timestamps of active steps.

## How It Works

### Execution
- `Execute(cmd_str, UserID)`: Parses command, backs up state, runs `Redo()`, saves to disk async.
- `PushJob()`: Queues I/O tasks—4 workers process via `IOWorker`.

### Undo/Redo
- `Undo()`: Steps back (`m_UndoIndex--`), loads `m_CacheUndoData` if needed, applies `Undo()`.
- `Redo()`: Steps forward (`m_UndoIndex++`), reapplies `Redo()`.

### Persistence
- `SaveTimestamps()`: On destroy, prunes `m_History` to `m_UndoIndex`, writes timestamps.
- `LoadTimestamps()`: On init, loads timestamps, queues `load_entries`, syncs, caches latest steps.

### Caching
- `UpdateLRU()`: Keeps `m_MaxCachedSteps=50` entries—prunes old, warms ahead/behind by `m_LookAheadSteps=5`.

### Example: `MoveCursor`
- `fake_dbase`: Tracks `m_X`, `m_Y`.
- `Redo()`: Sets new position.
- `Undo()`: Restores prior position from `m_CacheUndoData`.
- `BackupCurrenState()`: Saves current `m_X`, `m_Y`.

## Usage (Stress Test)

### First Run
1. Builds 500 commands (`Move -T 0 0` to `499 499`).
2. Undoes 100 (`m_UndoIndex=400`, `m_X=399`).
3. Saves 400 timestamps—destroys.

### Second Run
1. Loads 400 steps (`m_X=399` persists via shared `fake_dbase`).
2. Adds 50 (`1000-1049`, `m_X=1049`).
3. Undoes 20 (`m_X=1029`).
4. Inserts 10 mid-stack (`2000-2009`, `m_X=2009`).

### Output
```
After 10 mid-stack inserts at 430:
  [0399]-[U] ... Move -T 399 399
  [0400]-[U] ... Move -T 1000 1000
  ...
  [0429]-[U] ... Move -T 1029 1029
  [0430]-[U] ... Move -T 2000 2000
  ...
  [0439]-[U] ... Move -T 2009 2009
```

## Code Style
- **Variables**: Uppercase (`DataBase`, `Err`), lowercase loop vars (`i`).
- **Comments**: Top-line, explain intent—why, not just what.
- **Namespaces**: Full `std::`—no shortcuts.
- **Errors**: Scoped `if(auto Err=...)`—tight scope.
- **Formatting**: Crisp, aligned—readability first.

## Future Extensions
- **Redo Post-Insert**: Test redo after mid-stack—should fail (pruned history).
- **Disk Persistence**: Save/load `fake_dbase` to "Database.bin"—real app scenario.
- **5D History**: Branching timelines—multiple `m_History` vectors for parallel states.
- **Dynamic Threads**: Scale `m_IOThread` with history size (e.g., `min(Count/100, hardware_concurrency)`).
- **State Logging**: Print `fake_dbase.m_X`, `m_Y` in `displayHistory()`—track state live.

## Notes for Students & AIs
- **Concurrency**: 4 threads + `m_IOQueue`—watch `m_ActiveJobs` and `wait_for(100ms)` sync.
- **State**: Shared `fake_dbase` here—real apps might save/load it.
- **Extensibility**: Add commands—inherit `command_base`, override `Redo/Undo`.
- **Debug**: Log `m_IOQueue.size()`, `m_ActiveJobs`—see the guts work.

---