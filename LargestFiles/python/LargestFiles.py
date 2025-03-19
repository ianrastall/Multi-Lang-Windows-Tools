import os
import time
import ctypes

# Constants for drive types and file attributes
DRIVE_REMOVABLE = 2
DRIVE_FIXED = 3

FILE_ATTRIBUTE_HIDDEN = 0x2
FILE_ATTRIBUTE_SYSTEM = 0x4
FILE_ATTRIBUTE_TEMPORARY = 0x100
FILE_ATTRIBUTE_REPARSE_POINT = 0x400

def enumerate_drives():
    drives = []
    drive_mask = ctypes.windll.kernel32.GetLogicalDrives()
    for i in range(26):
        if drive_mask & (1 << i):
            drive_letter = chr(65 + i)
            drive = f"{drive_letter}:\\"
            drive_type = ctypes.windll.kernel32.GetDriveTypeW(drive)
            if drive_type in (DRIVE_REMOVABLE, DRIVE_FIXED):
                print(f"Including drive: {drive}")
                drives.append(drive)
    return drives

def should_skip(path):
    # Get file attributes; returns -1 on failure
    attrs = ctypes.windll.kernel32.GetFileAttributesW(path)
    if attrs == -1:
        return False
    # Skip if file is hidden, system, temporary or a reparse point (like symlink/junction)
    if attrs & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_REPARSE_POINT):
        return True
    return False

def scan_drive(drive):
    files = []
    start_time = time.time()
    for root, dirs, file_names in os.walk(drive):
        # Remove directories we want to skip from recursion
        dirs[:] = [d for d in dirs if not should_skip(os.path.join(root, d))]
        for file in file_names:
            full_path = os.path.join(root, file)
            if should_skip(full_path):
                continue
            try:
                size = os.path.getsize(full_path)
                files.append((full_path, size))
            except Exception:
                continue
    elapsed = time.time() - start_time
    return files, elapsed

def main():
    print("File Scanner")
    print("----------------------------------------")
    
    # Create/truncate the output file with UTF-8 encoding
    outfile = "largest_files.txt"
    with open(outfile, "w", encoding="utf-8") as f:
        pass

    drives = enumerate_drives()
    if not drives:
        print("No suitable drives found!")
        return

    for drive in drives:
        print(f"\nProcessing {drive}")
        files, elapsed = scan_drive(drive)
        print(f"Scanned {drive} in {elapsed:.1f} seconds")
        print(f"Found {len(files)} files")
        if not files:
            continue

        # Sort by size descending
        files.sort(key=lambda x: x[1], reverse=True)
        with open(outfile, "a", encoding="utf-8") as f:
            # Display drive as "C:" instead of "C:\\"
            f.write(f"Largest files on {drive[:2]}:\n")
            for path, size in files[:100]:
                mb = size / (1024 * 1024)
                f.write(f"{path}: {mb:.2f} MB\n")
            f.write("\n")

    print(f"\nScan complete. Results saved to {outfile}")
    input("Press Enter to exit...")

if __name__ == "__main__":
    main()
