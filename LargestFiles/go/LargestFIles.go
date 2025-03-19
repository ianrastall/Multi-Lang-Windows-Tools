package main

import (
    "fmt"
    "os"
    "path/filepath"
    "sort"
    "syscall"
    "time"
    "unsafe"
)

type FileEntry struct {
    Path string
    Size int64
}

const FILE_ATTRIBUTE_TEMPORARY = 0x100

var (
    modkernel32          = syscall.NewLazyDLL("kernel32.dll")
    procGetLogicalDrives = modkernel32.NewProc("GetLogicalDrives")
    procGetDriveTypeW    = modkernel32.NewProc("GetDriveTypeW")
)

func getLogicalDrives() (uint32, error) {
    ret, _, err := procGetLogicalDrives.Call()
    if ret == 0 {
        return 0, err
    }
    return uint32(ret), nil
}

func getDriveType(path string) uint32 {
    ptr, err := syscall.UTF16PtrFromString(path)
    if err != nil {
        return 0
    }
    ret, _, _ := procGetDriveTypeW.Call(uintptr(unsafe.Pointer(ptr)))
    return uint32(ret)
}

func enumerateDrives() []string {
    drives := []string{}
    mask, err := getLogicalDrives()
    if err != nil {
        return drives
    }

    fmt.Println("[Drive Scan]")
    for c := 'A'; c <= 'Z'; c++ {
        if mask&(1<<(c-'A')) != 0 {
            drive := fmt.Sprintf("%c:\\", c)
            dt := getDriveType(drive)
            // DRIVE_FIXED (3) or DRIVE_REMOVABLE (2)
            if dt == 3 || dt == 2 {
                fmt.Printf("Including drive: %s\n", drive)
                drives = append(drives, drive)
            } else {
                // If you want to see what’s being skipped, uncomment:
                // fmt.Printf("Skipping drive: %s (type %d)\n", drive, dt)
            }
        }
    }
    return drives
}

func scanDrive(drive string) []FileEntry {
    entries := []FileEntry{}

    walkFunc := func(path string, info os.FileInfo, err error) error {
        if err != nil {
            // Skip paths we can't read/access
            return nil
        }

        // If it's a directory, just keep going
        if info.IsDir() {
            return nil
        }

        // Otherwise, add the file
        entries = append(entries, FileEntry{
            Path: path,
            Size: info.Size(),
        })
        return nil
    }

    filepath.Walk(drive, walkFunc)
    return entries
}

func main() {
    fmt.Println("File Scanner")
    fmt.Println("----------------------------------------")

    drives := enumerateDrives()
    if len(drives) == 0 {
        fmt.Fprintln(os.Stderr, "No suitable drives found!")
        os.Exit(1)
    }

    outfile := "largest_files.txt"
    // Create or truncate the file
    os.WriteFile(outfile, []byte{}, 0644)

    for _, drive := range drives {
        fmt.Printf("\nProcessing %s\n", drive)

        start := time.Now()
        entries := scanDrive(drive)
        elapsed := time.Since(start)
        fmt.Printf("Scanned %s in %.1f seconds\n", drive, elapsed.Seconds())
        fmt.Printf("Found %d files\n", len(entries))

        if len(entries) == 0 {
            continue
        }

        // Sort by size descending
        sort.Slice(entries, func(i, j int) bool {
            return entries[i].Size > entries[j].Size
        })

        // Grab the top 100
        limit := 100
        if len(entries) < limit {
            limit = len(entries)
        }
        topEntries := entries[:limit]

        // Append results to the output file
        f, err := os.OpenFile(outfile, os.O_APPEND|os.O_WRONLY, 0644)
        if err != nil {
            fmt.Printf("Failed to open output file: %v\n", err)
            continue
        }

        driveDisplay := drive[:2] // e.g. "C:"
        fmt.Fprintf(f, "Largest files on %s:\n", driveDisplay)
        for _, entry := range topEntries {
            mb := float64(entry.Size) / (1024 * 1024)
            fmt.Fprintf(f, "%s: %.2f MB\n", entry.Path, mb)
        }
        fmt.Fprintln(f) // empty line between drives
        f.Close()
    }

    fmt.Printf("\nScan complete. Results saved to %s\n", outfile)
    fmt.Println("Press Enter to exit...")
    fmt.Scanln()
}
