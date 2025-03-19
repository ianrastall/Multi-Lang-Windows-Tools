import core.sys.windows.windows;
import core.stdc.stdio : printf, fprintf, fflush, freopen, getchar, stdout, stderr;
import std.string : fromStringz, toStringz;
import std.algorithm : sort;
import std.stdio : File, writefln;
import std.format : format;
import std.exception : enforce;
import std.range;

struct FileEntry {
    string path;
    ulong size;
}

void initConsole() {
    if (GetConsoleWindow() is null) {
        AllocConsole();
        enforce(freopen("CONOUT$", "w", stdout) !is null, "Failed to reopen stdout");
        enforce(freopen("CONOUT$", "w", stderr) !is null, "Failed to reopen stderr");
    }
}

string[] enumerateDrives() {
    string[] drives;
    DWORD mask = GetLogicalDrives();
    printf("[Drive Scan]\n");
    foreach (c; 'A' .. ('Z' + 1)) {
        if (mask & (1 << (c - 'A'))) {
            // Use toStringz() to ensure the string is null-terminated for printf
            string drive = format("%c:\\", cast(char)c);
            UINT type = GetDriveTypeA(toStringz(drive));
            if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE) {
                printf("Including drive: %s\n", toStringz(drive));
                drives ~= drive;
            }
        }
    }
    return drives;
}

void processDirectory(string path, ref FileEntry[] files) {
    string searchPath;
    if (path.empty) {
        searchPath = "*";
    } else if (path[$-1] == '\\' || path[$-1] == '/') {
        searchPath = path ~ "*";
    } else {
        searchPath = path ~ "\\*";
    }
    
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(toStringz(searchPath), &findData);
    
    if (hFind == INVALID_HANDLE_VALUE) return;
    
    do {
        string fileName = cast(immutable) fromStringz(findData.cFileName.ptr);
        if (fileName == "." || fileName == "..") continue;
        
        if (findData.dwFileAttributes & (FILE_ATTRIBUTE_SYSTEM | 
                                         FILE_ATTRIBUTE_HIDDEN | 
                                         FILE_ATTRIBUTE_TEMPORARY))
            continue;
        
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
            continue;
        
        string fullPath;
        if (path.empty) {
            fullPath = fileName;
        } else if (path[$-1] == '\\' || path[$-1] == '/') {
            fullPath = path ~ fileName;
        } else {
            fullPath = path ~ "\\" ~ fileName;
        }
        
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            processDirectory(fullPath, files);
        } else {
            ulong size = (cast(ulong)findData.nFileSizeHigh << 32) | findData.nFileSizeLow;
            files ~= FileEntry(fullPath, size);
        }
    } while (FindNextFileA(hFind, &findData));
    
    FindClose(hFind);
}

void main() {
    initConsole();
    printf("File Scanner\n");
    printf("----------------------------------------\n");
    
    string[] drives = enumerateDrives();
    if (drives.empty) {
        fprintf(stderr, "No suitable drives found!\n");
        return;
    }
    
    string outfile = "largest_files.txt";
    auto outputFile = File(outfile, "w");
    outputFile.close();
    
    foreach (drive; drives) {
        // Use toStringz() for console output to avoid extra garbage text
        printf("\nProcessing %s\n", toStringz(drive));
        FileEntry[] files;
        
        DWORD startTime = GetTickCount();
        processDirectory(drive, files);
        DWORD duration = GetTickCount() - startTime;
        
        printf("Scanned %s in %.1f seconds\n", toStringz(drive), duration / 1000.0);
        printf("Found %zu files\n", files.length);
        
        if (files.empty) continue;
        
        files.sort!((a, b) => a.size > b.size);
        
        outputFile = File(outfile, "a");
        scope(exit) outputFile.close();
        
        string driveDisplay = drive[0 .. 2];
        outputFile.writefln("Largest files on %s:", driveDisplay);
        
        size_t count = files.length < 100 ? files.length : 100;
        foreach (i; 0 .. count) {
            double sizeMB = cast(double)files[i].size / (1024.0 * 1024.0);
            outputFile.writefln("%s: %.2f MB", files[i].path, sizeMB);
        }
        outputFile.writefln("");
    }
    
    printf("\nScan complete. Results saved to %s\n", toStringz(outfile));
    printf("Press Enter to exit...");
    fflush(stdout);
    while (getchar() != '\n') {}
}
