# cue2pops.py (Python Version)

This project is a refactored and optimized Python implementation of **cue2pops**, designed to run cross-platform, including Android devices via Termux.

## Key Improvements

The original C codebase has been completely refactored into modern Python with a focus on code safety, maintainability, and clean execution.

* **Native Python Implementation:** No C compilation required—runs directly with Python 3.
* **Robust File Handling:** Powered by `pathlib` for safe cross-platform path resolution and automatic input/output directory creation.
* **Smart Resource Management:** Uses contextual file streaming to prevent memory leaks and optimize I/O performance.
* **Process Interruption Safety:** Supports graceful termination (`SIGINT`/`SIGTERM`) with atomic temporary file replacements to prevent corrupted output files.
* **Game Patching & Auto-Detection:** Retains built-in support for game signature identification, trainer injections, and compatibility fixes.

## Requirements

* Python 3.8 or higher

## Usage

Convert a `.cue` file to `.VCD`:

```bash
python rcue2pops.py "path/to/file.cue"
