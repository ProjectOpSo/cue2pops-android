# cue2pops (android / termux)

This project is an optimized version of **cue2pops** designed specifically to run on Android devices using the Termux environment.

## What is this optimization?

The original tool has been modified and patched to function reliably within the Android Linux emulation environment. 

### Key Improvements:
* **Path Handling:** Fixed support for folders and file names containing spaces.
* **CUE/BIN Parsing:** Corrected the parser to prevent location errors when identifying the target `.bin` file.
* **Directory Support:** Added native support for automatic output directory creation (`--output`).
* **Error Messages:** Enhanced verbose terminal output for easier troubleshooting during execution failures.

## Compilation

To compile the application inside Termux, run:

```bash
make
```

## Usage

Convert a game by running:

```bash
./cue2pops "path/to/file.cue"
```

Or specify a custom output directory:

```bash
./cue2pops --output "path/to/folder/" "path/to/file.cue"
```
