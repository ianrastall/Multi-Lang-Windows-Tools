/*
Compile with:
g++ -o LargestFiles LargestFiles.cpp -O2 -Wall

This C++ port maintains all functionality from the C version while leveraging
C++ standard library features for cleaner resource management and path handling.
*/

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>

struct FileEntry {
    std::string path;
    uint64_t size;
    
    FileEntry(const std::string& p, uint64_t s) 
        : path(p), size(s) {}
};

using FileList = std::vector<FileEntry>;

// Initialize console for GUI applications
void init_console() {
    if (!GetConsoleWindow()) {
        AllocConsole();
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
}

// Get list of available fixed/removable drives
std::vector<std::string> enumerate_drives() {
    std::vector<std::string> drives;
    DWORD mask = GetLogicalDrives();
    
    std::printf("[Drive Scan]\n");
    for (char c = 'A'; c <= 'Z'; ++c) {
        if (mask & (1 << (c - 'A'))) {
            std::string drive(1, c);
            drive += ":\\";  // Windows requires trailing slash for drive paths
            UINT type = GetDriveTypeA(drive.c_str());
            
            if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE) {
                std::printf("Including drive: %s\n", drive.c_str());
                drives.push_back(drive);
            }
        }
    }
    return drives;
}

// Recursive directory scanner with proper path construction
void process_directory(const std::string& path, FileList& files) {
    std::string search;
    if (path.empty()) {
        search = "*";
    } else if (path.back() == '\\' || path.back() == '/') {
        search = path + "*";
    } else {
        search = path + "\\*";
    }

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search.c_str(), &fd);
    
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        // Skip special entries and system files
        if (std::strcmp(fd.cFileName, ".") == 0 || 
            std::strcmp(fd.cFileName, "..") == 0 ||
            (fd.dwFileAttributes & (FILE_ATTRIBUTE_SYSTEM | 
                                   FILE_ATTRIBUTE_HIDDEN | 
                                   FILE_ATTRIBUTE_TEMPORARY))) {
            continue;
        }

        // Build full path with proper separator
        std::string fullPath;
        if (path.empty()) {
            fullPath = fd.cFileName;
        } else if (path.back() == '\\' || path.back() == '/') {
            fullPath = path + fd.cFileName;
        } else {
            fullPath = path + "\\" + fd.cFileName;
        }

        // Skip reparse points (symbolic links/junctions)
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            continue;
        }

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            process_directory(fullPath, files);
        } else {
            // Store file with single backslash paths
            uint64_t fileSize = (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
            files.emplace_back(fullPath, fileSize);
        }
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);
}

int main() {
    init_console();
    std::printf("File Scanner\n");
    std::printf("----------------------------------------\n");

    auto drives = enumerate_drives();
    if (drives.empty()) {
        std::fprintf(stderr, "No suitable drives found!\n");
        return 1;
    }

    const std::string outfile = "largest_files.txt";
    std::ofstream outputFile(outfile, std::ios::trunc);
    outputFile.close();

    for (const auto& drive : drives) {
        std::printf("\nProcessing %s\n", drive.c_str());
        FileList files;

        DWORD startTime = GetTickCount();
        process_directory(drive, files);
        DWORD duration = GetTickCount() - startTime;

        std::printf("Scanned %s in %.1f seconds\n", drive.c_str(), duration / 1000.0);
        std::printf("Found %zu files\n", files.size());

        if (files.empty()) continue;

        // Sort descending by file size
        std::sort(files.begin(), files.end(), [](const FileEntry& a, const FileEntry& b) {
            return a.size > b.size;
        });

        // Write results with clean formatting
        std::ofstream outputFile(outfile, std::ios::app);
        if (outputFile) {
            // Display drive as "C:" instead of "C:\\"
            outputFile << "Largest files on " << drive.substr(0, 2) << ":\n";
            const size_t count = std::min(files.size(), static_cast<size_t>(100));
            
            for (size_t i = 0; i < count; ++i) {
                const double sizeMB = files[i].size / (1024.0 * 1024.0);
                outputFile << files[i].path << ": " 
                          << std::fixed << std::setprecision(2) 
                          << sizeMB << " MB\n";
            }
            outputFile << "\n";
        }
    }

    std::printf("\nScan complete. Results saved to %s\n", outfile.c_str());
    std::printf("Press Enter to exit...");
    std::cin.get();

    return 0;
}