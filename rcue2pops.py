import argparse
import errno
import os
import re
import signal
import sys
import shutil
from dataclasses import dataclass
from pathlib import Path
from typing import Optional, Tuple

# Constants defining sector size, header size, and I/O buffer allocation
SECTORSIZE = 2352
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
    no_sync: bool = False
    
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


def write_vcd_header(vcd_file, total_sectors: int):
    """
    Writes the 1 MiB (0x100000) VCD header without allocating a 1 MiB buffer in RAM.
    Uses a small buffer for zero-filling and writes the exact same header offsets.
    """
    CHUNK_SIZE = 65536  # Small 64 KiB buffer for efficient chunked writing
    zeros = bytearray(CHUNK_SIZE)

    # Block 0 (First 64 KiB)
    block0 = bytearray(CHUNK_SIZE)
    block0[0] = 0x41
    block0[2] = 0xA0
    block0[7] = 0x01
    block0[8] = 0x20
    block0[12] = 0xA1
    block0[22] = 0xA2

    # Insert sector count indicators into offsets 1032 and 1036
    block0[1032] = total_sectors & 0xFF
    block0[1033] = (total_sectors >> 8) & 0xFF
    block0[1034] = (total_sectors >> 16) & 0xFF

    block0[1036] = total_sectors & 0xFF
    block0[1037] = (total_sectors >> 8) & 0xFF
    block0[1038] = (total_sectors >> 16) & 0xFF

    # Write the initial 64 KiB
    vcd_file.write(block0)

    # Write the remainder of the 1 MiB header filled with zeros (15 * 64 KiB = 960 KiB)
    for _ in range(15):
        vcd_file.write(zeros)


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
    Reads the CUE file line-by-line (without reading the entire file into RAM) and extracts directives.
    """
    extracted_bin = None
    track_count = 0
    index1_count = 0
    pregap_count = 0
    postgap_count = 0

    re_file = re.compile(r'^\s*FILE\s+(?:"([^"]+)"|(\S+))', re.IGNORECASE)
    re_track = re.compile(r'^\s*TRACK\s+\d+', re.IGNORECASE)
    re_index1 = re.compile(r'^\s*INDEX\s+01\b', re.IGNORECASE)
    re_pregap = re.compile(r'^\s*PREGAP\b', re.IGNORECASE)
    re_postgap = re.compile(r'^\s*POSTGAP\b', re.IGNORECASE)

    with open(cue_path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            trimmed = line.strip()
            if not trimmed or trimmed.upper().startswith("REM"):
                continue

            if not extracted_bin:
                m_file = re_file.match(trimmed)
                if m_file:
                    extracted_bin = m_file.group(1) or m_file.group(2)

            if re_track.match(trimmed):
                track_count += 1
            elif re_index1.match(trimmed):
                index1_count += 1
            elif re_pregap.match(trimmed):
                pregap_count += 1
            elif re_postgap.match(trimmed):
                postgap_count += 1

    if not extracted_bin:
        raise ValueError("Invalid CUE format: FILE directive not found")

    if track_count == 0 or track_count != index1_count:
        raise ValueError("Invalid CUE structure: TRACK / INDEX 01 count mismatch")

    return extracted_bin, track_count, index1_count, pregap_count, postgap_count


def convert_cue_to_vcd(params: Parameters, cue_name: str, vcd_name: Optional[str], out_dir: Optional[str]) -> int:
    """
    Executes the conversion while maintaining isolation via .tmp, optimized I/O, and thorough error handling.
    """
    global g_interrupted

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

    try:
        bin_size = bin_path.stat().st_size
    except OSError as e:
        log_error("Error reading BIN file info", e.strerror)
        return 1

    if bin_size % SECTORSIZE != 0:
        print(
            f"[WARNING] BIN file size ({bin_size} bytes) is not an exact multiple of SECTORSIZE "
            f"({SECTORSIZE}). Remainder: {bin_size % SECTORSIZE} bytes."
        )

    base_vcd_name = cue_path.stem + ".VCD"

    # Define target output VCD file
    if out_dir:
        out_dir_path = Path(out_dir)
        out_dir_path.mkdir(parents=True, exist_ok=True)
        final_vcd_path = out_dir_path / base_vcd_name
    elif vcd_name:
        final_vcd_path = Path(vcd_name)
    else:
        final_vcd_path = dir_part / base_vcd_name

    # Ensure original VCD is not overwritten without explicit permission
    if final_vcd_path.exists() and not params.force_overwrite:
        sys.stderr.write(
            f"[ERROR] Target file already exists: {final_vcd_path} (use -f or --force to overwrite)\n"
        )
        return 1

    tmp_vcd_path = Path(str(final_vcd_path) + ".tmp")
    if tmp_vcd_path.exists():
        try:
            tmp_vcd_path.unlink()
        except OSError as e:
            log_error("Cannot remove pre-existing temporary file", e.strerror)
            return 1

    # Pre-conversion disk space verification
    required_space = bin_size + VCD_HEADER_SIZE
    if not check_disk_space(tmp_vcd_path, required_space):
        return 1

    created_tmp = False

    try:
        with open(bin_path, "rb") as bin_file, open(tmp_vcd_path, "wb") as vcd_file:
            created_tmp = True

            total_sectors = (bin_size // SECTORSIZE) + (150 * (pregap_count + postgap_count))
            if total_sectors > 0xFFFFFF:
                sys.stderr.write("[ERROR] Sector count exceeds VCD header limit.\n")
                return 1

            # Write the 1 MiB header without giant RAM allocations
            write_vcd_header(vcd_file, total_sectors)

            # Single reusable 256 KiB RAM buffer for the entire I/O lifecycle
            io_buffer = bytearray(IO_BUFFER_SIZE)

            # Process and patch the first block
            n_first = bin_file.readinto(io_buffer)
            if n_first > 0:
                first_view = io_buffer[:n_first]
                game_identifier(first_view, params)
                game_fixer(first_view, params)
                game_trainer(first_view, params)
                vcd_file.write(first_view)

            # Main transfer loop using readinto with the same io_buffer instance
            while not g_interrupted:
                n_read = bin_file.readinto(io_buffer)
                if n_read == 0:
                    break
                vcd_file.write(io_buffer[:n_read])

            if g_interrupted:
                sys.stderr.write("\n[INFO] Conversion interrupted by user.\n")
                return 1

            vcd_file.flush()
            if not params.no_sync:
                os.fsync(vcd_file.fileno())

        # Atomic replacement: move from VCD.tmp to final target VCD
        tmp_vcd_path.replace(final_vcd_path)
        print(f"[OK] Converted successfully: {final_vcd_path}")
        return 0

    except OSError as e:
        if e.errno == errno.ENOSPC:
            sys.stderr.write("[ERROR] Conversion failed: Insufficient disk space (ENOSPC).\n")
        elif e.errno == errno.ENOMEM:
            sys.stderr.write("[ERROR] Conversion failed: Out of memory (ENOMEM).\n")
        elif e.errno == errno.EACCES:
            sys.stderr.write("[ERROR] Conversion failed: Permission denied (EACCES).\n")
        elif e.errno == errno.EIO:
            sys.stderr.write("[ERROR] Conversion failed: Physical I/O error (EIO).\n")
        else:
            log_error("I/O error during conversion process", e.strerror)

        if created_tmp and tmp_vcd_path.exists():
            try:
                tmp_vcd_path.unlink()
            except OSError:
                pass
        return 1

    except Exception as e:
        log_error("Unexpected error during conversion process", str(e))
        if created_tmp and tmp_vcd_path.exists():
            try:
                tmp_vcd_path.unlink()
            except OSError:
                pass
        return 1

    finally:
        # Guarantee cleanup of .tmp if the process was interrupted or failed
        if (g_interrupted or not final_vcd_path.exists()) and created_tmp and tmp_vcd_path.exists():
            try:
                tmp_vcd_path.unlink()
            except OSError:
                pass


def main():
    """
    Main entry point and CLI argument parser configuration.
    """
    parser = argparse.ArgumentParser(description="BIN/CUE to IMAGE0.VCD conversion tool (POPS)")
    parser.add_argument("cue_name", help="Input .cue file path")
    parser.add_argument("vcd_name", nargs="?", default=None, help="Output .vcd file path (optional)")
    parser.add_argument("-o", "--output", dest="out_dir", help="Custom output directory")
    parser.add_argument("-f", "--force", action="store_true", help="Overwrite existing output VCD")
    parser.add_argument("--no-sync", action="store_true", help="Disable final fsync for maximum speed")
    parser.add_argument("--debug-cue", action="store_true", help="Detailed CUE/BIN path debugging")
    parser.add_argument("extra_args", nargs="*", help="Additional legacy options (vmode, trainer, gap++, gap--)")

    args = parser.parse_args()

    params = Parameters()
    params.debug_cue = args.debug_cue
    params.force_overwrite = args.force
    params.no_sync = args.no_sync

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
