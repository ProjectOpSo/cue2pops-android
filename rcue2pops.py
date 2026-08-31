import argparse
import os
import re
import signal
import sys
import shutil
from dataclasses import dataclass
from pathlib import Path
from typing import Optional, Tuple

# Constants defining sector sizes and buffer allocations
SECTOR_SIZE = 2352
VCD_HEADER_SIZE = 0x100000
IO_BUFFER_SIZE = 0x40000

# Global flag for graceful process signal interruption
g_interrupted = False


def handle_signal(sig, frame):
    """
    Signal handler to set the interruption flag when a termination signal (SIGINT/SIGTERM) is received.
    """
    global g_interrupted
    g_interrupted = True


# Register OS signals for graceful termination
signal.signal(signal.SIGINT, handle_signal)
signal.signal(signal.SIGTERM, handle_signal)


@dataclass
class Parameters:
    """
    Holds configuration flags and internal state values required for processing.
    """
    vmode: bool = False
    trainer: bool = False
    gap_more: bool = False
    gap_less: bool = False
    debug_cue: bool = False
    force_overwrite: bool = False
    
    deny_vmode: int = 0
    fix_game: int = 0
    game_has_cheats: int = 0
    game_title: int = 0
    game_trained: int = 0
    game_fixed: int = 0


@dataclass
class GameSignature:
    """
    Structure representing a specific PS1 game signature for auto-identification and patching.
    """
    sig: bytes
    name: str
    title_id: int
    deny_vmode: int


# List of known game signatures for automatic patch application
GAME_SIGNATURES = [
    GameSignature(b"SCES-00344", "Crash Bandicoot [SCES-00344]", 1, 1),
    GameSignature(b"SCUS-94900", "Crash Bandicoot [SCUS-94900]", 2, 0),
    GameSignature(b"SCPS-10031", "Crash Bandicoot [SCPS-10031]", 3, 0),
]


def log_error(msg: str, detail: Optional[str] = None):
    """
    Prints structured error messages to standard error (stderr).
    """
    if detail:
        sys.stderr.write(f"[ERROR] {msg}: {detail}\n")
    else:
        sys.stderr.write(f"[ERROR] {msg}\n")


def check_disk_space(path: Path, required_bytes: int) -> bool:
    """
    Checks if the destination filesystem has enough available storage for the output file.
    """
    try:
        parent_dir = path.parent if not path.exists() else path
        usage = shutil.disk_usage(parent_dir)
        if usage.free < required_bytes:
            sys.stderr.write(
                f"[ERROR] Insufficient disk space. Required: {required_bytes} bytes, "
                f"Available: {usage.free} bytes\n"
            )
            return False
        return True
    except Exception:
        # Fallback to true if disk space inspection is not supported by the filesystem
        return True


def game_identifier(inbuf: bytearray, p: Parameters):
    """
    Scans the initial buffer to identify specific game titles based on binary signatures.
    """
    if p.debug_cue:
        if not p.vmode:
            print("-" * 82)
        print("Hello from game_identifier!")

    if p.game_title == 0:
        for ptr in range(0, min(len(inbuf), IO_BUFFER_SIZE) - 16, 4):
            for gsig in GAME_SIGNATURES:
                if inbuf[ptr : ptr + len(gsig.sig)] == gsig.sig:
                    print("-" * 82)
                    print(gsig.name)
                    if gsig.deny_vmode:
                        p.deny_vmode += 1
                    p.game_title = gsig.title_id
                    p.game_has_cheats = 1
                    p.fix_game = 0
                    break
            if p.game_title != 0:
                break

    if p.game_title != 0:
        if p.fix_game == 1:
            print("GameFixer is ON")
        if p.trainer and p.game_has_cheats == 0:
            print("There is no cheat for this title")
        if p.deny_vmode != 0 and p.vmode:
            print("VMODE patching is disabled for this title")


def game_fixer(inbuf: bytearray, p: Parameters):
    """
    Applies compatibility fixes to specific game binary structures if required.
    """
    if p.game_fixed == 0:
        for ptr in range(0, min(len(inbuf), IO_BUFFER_SIZE) - 8, 4):
            if p.game_title == 4:
                if inbuf[ptr : ptr + 4] == b"\x78\x26\x43\x8c":
                    inbuf[ptr] = 0x74
                if inbuf[ptr : ptr + 4] == b"\xe8\x75\x06\x80":
                    if ptr >= 8:
                        inbuf[ptr - 8] = 0x07
                        print("game_fixer : Disc Swap Patched")
                        p.game_fixed = 1
                        break


def game_trainer(inbuf: bytearray, p: Parameters):
    """
    Applies built-in cheat or system modifications (trainers) based on the game title.
    """
    if p.game_trained == 0:
        for ptr in range(0, min(len(inbuf), IO_BUFFER_SIZE) - 4, 4):
            if p.game_title == 1 and inbuf[ptr : ptr + 4] == b"\x7c\x16\x20\xac":
                inbuf[ptr + 2] = 0x22
                print("game_trainer : Test Save System Enabled")
                p.game_trained = 1
                break
            elif p.game_title == 2 and inbuf[ptr : ptr + 4] == b"\x9c\x19\x20\xac":
                inbuf[ptr + 2] = 0x22
                print("game_trainer : Test Save System Enabled")
                p.game_trained = 1
                break
            elif p.game_title == 3 and inbuf[ptr : ptr + 4] == b"\x84\x19\x20\xac":
                inbuf[ptr + 2] = 0x22
                print("game_trainer : Test Save System Enabled")
                p.game_trained = 1
                break


def parse_cue_file(cue_path: Path) -> Tuple[str, int, int, int, int]:
    """
    Parses a .CUE sheet file to extract the associated BIN filename and calculate track metadata.
    """
    with open(cue_path, "r", encoding="utf-8", errors="ignore") as f:
        lines = f.readlines()

    extracted_bin = None
    track_count = 0
    index1_count = 0
    pregap_count = 0
    postgap_count = 0

    for line in lines:
        trimmed = line.strip()
        if trimmed.upper().startswith("REM"):
            continue

        # Extract the target BIN filename from the FILE directive
        if not extracted_bin and trimmed.upper().startswith("FILE"):
            match = re.search(r'FILE\s+"([^"]+)"', line, re.IGNORECASE)
            if not match:
                match = re.search(r'FILE\s+(\S+)', line, re.IGNORECASE)
            if match:
                extracted_bin = match.group(1)

        upper_line = trimmed.upper()
        if "TRACK" in upper_line:
            track_count += 1
        if "INDEX 01" in upper_line:
            index1_count += 1
        if "PREGAP" in upper_line:
            pregap_count += 1
        if "POSTGAP" in upper_line:
            postgap_count += 1

    if not extracted_bin:
        raise ValueError("Invalid CUE format: FILE directive not found")

    if track_count == 0 or track_count != index1_count:
        raise ValueError("Invalid CUE structure: Track count mismatch")

    return extracted_bin, track_count, index1_count, pregap_count, postgap_count


def convert_cue_to_vcd(params: Parameters, cue_name: str, vcd_name: Optional[str], out_dir: Optional[str]) -> int:
    """
    Core conversion pipeline from BIN/CUE to POPS VCD format.
    """
    cue_path = Path(cue_name).resolve()
    if not cue_path.exists():
        log_error("Cannot open or access CUE file", cue_name)
        print(f"[FAIL] {cue_name}")
        return 1

    dir_part = cue_path.parent

    if params.debug_cue:
        print(f"[DEBUG] CUE Path: {cue_path}")
        print(f"[DEBUG] CUE Directory: {dir_part}")

    try:
        extracted_bin, track_count, index1_count, pregap_count, postgap_count = parse_cue_file(cue_path)
    except Exception as e:
        log_error(str(e), cue_name)
        print(f"[FAIL] {cue_name}")
        return 1

    if params.debug_cue:
        print(f"[DEBUG] FILE directive extracted: {extracted_bin}")

    bin_path = Path(extracted_bin)
    if not bin_path.is_absolute():
        bin_path = dir_part / extracted_bin

    if params.debug_cue:
        print(f"[DEBUG] Resolved BIN Path: {bin_path}")

    if not bin_path.exists():
        log_error("Cannot locate or read associated BIN file", str(bin_path))
        print(f"[FAIL] {cue_name}")
        return 1

    bin_size = bin_path.stat().st_size
    if bin_size % SECTOR_SIZE != 0:
        print(
            f"[WARNING] BIN file size ({bin_size} bytes) is not an exact multiple of SECTORSIZE "
            f"({SECTOR_SIZE}). Remainder: {bin_size % SECTOR_SIZE} bytes."
        )

    base_vcd_name = cue_path.stem + ".VCD"

    # Resolve target output paths
    if out_dir:
        out_dir_path = Path(out_dir)
        out_dir_path.mkdir(parents=True, exist_ok=True)
        final_vcd_path = out_dir_path / base_vcd_name
    elif vcd_name:
        final_vcd_path = Path(vcd_name)
    else:
        final_vcd_path = dir_part / base_vcd_name

    # Check for target file collision
    if final_vcd_path.exists() and not params.force_overwrite:
        sys.stderr.write(
            f"[ERROR] Target file already exists: {final_vcd_path} (use -f or --force to overwrite)\n"
        )
        return 1

    tmp_vcd_path = Path(str(final_vcd_path) + ".tmp")
    if tmp_vcd_path.exists():
        tmp_vcd_path.unlink()

    # Verify storage requirements
    if not check_disk_space(tmp_vcd_path, bin_size + VCD_HEADER_SIZE):
        return 1

    try:
        with open(bin_path, "rb") as bin_file, open(tmp_vcd_path, "wb") as vcd_file:
            # Build and write POPS 1MB VCD header
            headerbuf = bytearray(VCD_HEADER_SIZE)
            headerbuf[0] = 0x41
            headerbuf[2] = 0xA0
            headerbuf[7] = 0x01
            headerbuf[8] = 0x20
            headerbuf[12] = 0xA1
            headerbuf[22] = 0xA2

            total_sectors = (bin_size // SECTOR_SIZE) + (150 * (pregap_count + postgap_count))
            if total_sectors > 0xFFFFFF:
                sys.stderr.write("[ERROR] Sector count exceeds VCD header limit.\n")
                return 1

            # Populate sector count indicators inside header
            headerbuf[1032] = total_sectors & 0xFF
            headerbuf[1033] = (total_sectors >> 8) & 0xFF
            headerbuf[1034] = (total_sectors >> 16) & 0xFF

            headerbuf[1036] = total_sectors & 0xFF
            headerbuf[1037] = (total_sectors >> 8) & 0xFF
            headerbuf[1038] = (total_sectors >> 16) & 0xFF

            vcd_file.write(headerbuf)
            del headerbuf  # Free header memory buffer immediately

            # Process and patch the first I/O block
            first_block = bytearray(bin_file.read(IO_BUFFER_SIZE))
            if first_block:
                game_identifier(first_block, params)
                game_fixer(first_block, params)
                game_trainer(first_block, params)
                vcd_file.write(first_block)

            # Main I/O transfer loop
            while not g_interrupted:
                chunk = bin_file.read(IO_BUFFER_SIZE)
                if not chunk:
                    break
                vcd_file.write(chunk)

            if g_interrupted:
                sys.stderr.write("\n[INFO] Conversion interrupted by user.\n")
                return 1

            vcd_file.flush()
            os.fsync(vcd_file.fileno())

        # Atomic move from temporary file to final target VCD
        tmp_vcd_path.replace(final_vcd_path)
        print(f"[OK] Converted: {final_vcd_path}")
        return 0

    except Exception as e:
        log_error("Error during conversion process", str(e))
        if tmp_vcd_path.exists():
            tmp_vcd_path.unlink()
        return 1


def main():
    """
    Main entry point and CLI argument parser configuration.
    """
    parser = argparse.ArgumentParser(description="BIN/CUE to IMAGE0.VCD conversion tool (Python Version)")
    parser.add_argument("cue_name", help="Input .cue file path")
    parser.add_argument("vcd_name", nargs="?", default=None, help="Output .vcd file path (optional)")
    parser.add_argument("-o", "--output", dest="out_dir", help="Custom output directory")
    parser.add_argument("-f", "--force", action="store_true", help="Overwrite existing output VCD")
    parser.add_argument("--debug-cue", action="store_true", help="Detailed CUE/BIN path debugging")
    parser.add_argument("extra_args", nargs="*", help="Additional legacy options (vmode, trainer, gap++, gap--)")

    args = parser.parse_args()

    params = Parameters()
    params.debug_cue = args.debug_cue
    params.force_overwrite = args.force

    # Parse positional legacy flags
    for arg in args.extra_args:
        if arg == "gap++":
            params.gap_more = True
        elif arg == "gap--":
            params.gap_less = True
        elif arg == "vmode":
            params.vmode = True
        elif arg == "trainer":
            params.trainer = True

    sys.exit(convert_cue_to_vcd(params, args.cue_name, args.vcd_name, args.out_dir))


if __name__ == "__main__":
    main()
