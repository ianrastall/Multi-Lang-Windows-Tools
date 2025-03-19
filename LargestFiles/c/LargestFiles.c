/*
Compile with:
gcc -o LargestFiles LargestFiles.c -O2 -Wall

This program scans fixed and removable drives, lists the largest files,
and saves the results to 'largest_files.txt'. It avoids double backslashes
in output paths and includes detailed comments for clarity.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <windows.h>

#define MAX_PATH_BUFFER 32767  // Maximum path length for Windows API

// Structure to hold file path and size information
typedef struct {
    char *path;
    uint64_t size;
} FileEntry;

// Dynamic array to store file entries
typedef struct {
    FileEntry *entries;
    size_t size;      // Current number of entries
    size_t capacity;  // Allocated capacity
} FileList;

// Initialize console if not attached (for GUI applications)
void init_console() {
    if (!GetConsoleWindow()) {
        AllocConsole();
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
}

// Initialize a FileList structure
void file_list_init(FileList *list) {
    list->entries = NULL;
    list->size = 0;
    list->capacity = 0;
}

// Add a file entry to the FileList, dynamically resizing as needed
int file_list_add(FileList *list, const char *path, uint64_t size) {
    if (list->size >= list->capacity) {
        size_t new_cap = list->capacity ? list->capacity * 2 : 128;
        FileEntry *new_entries = realloc(list->entries, new_cap * sizeof(FileEntry));
        if (!new_entries) return 0;
        list->entries = new_entries;
        list->capacity = new_cap;
    }
    
    // Duplicate path and store in list
    char *path_copy = strdup(path);
    if (!path_copy) return 0;
    
    list->entries[list->size].path = path_copy;
    list->entries[list->size].size = size;
    list->size++;
    return 1;
}

// Free resources used by a FileList
void file_list_free(FileList *list) {
    for (size_t i = 0; i < list->size; i++)
        free(list->entries[i].path);
    free(list->entries);
    list->entries = NULL;
    list->size = list->capacity = 0;
}

// Comparator for sorting files by size (descending)
int compare_entries(const void *a, const void *b) {
    const FileEntry *ea = (const FileEntry *)a;
    const FileEntry *eb = (const FileEntry *)b;
    return (ea->size > eb->size) ? -1 : (ea->size < eb->size) ? 1 : 0;
}

// Get list of available drives (fixed and removable)
int enumerate_drives(char ***drives, size_t *count) {
    DWORD mask = GetLogicalDrives();
    *drives = NULL;
    *count = 0;
    
    printf("[Drive Scan]\n");
    for (char c = 'A'; c <= 'Z'; c++) {
        if (mask & (1 << (c - 'A'))) {
            char drive[4];
            snprintf(drive, sizeof(drive), "%c:\\", c);  // Format as "C:\\"
            UINT type = GetDriveTypeA(drive);
            
            if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE) {
                // Add valid drive to list
                printf("Including drive: %s\n", drive);
                char **new_drives = realloc(*drives, (*count + 1) * sizeof(char *));
                if (!new_drives) {
                    for (size_t i = 0; i < *count; i++) free((*drives)[i]);
                    free(*drives);
                    return 0;
                }
                *drives = new_drives;
                (*drives)[*count] = strdup(drive);
                if (!(*drives)[*count]) {
                    for (size_t i = 0; i < *count; i++) free((*drives)[i]);
                    free(*drives);
                    return 0;
                }
                (*count)++;
            }
        }
    }
    return 1;
}

// Recursive directory traversal with proper path handling
void process_win(const char *path, FileList *list) {
    char search[MAX_PATH_BUFFER];
    size_t path_len = strlen(path);
    
    // Handle trailing slash for search pattern
    if (path_len > 0 && (path[path_len - 1] == '\\' || path[path_len - 1] == '/')) {
        snprintf(search, sizeof(search), "%s*", path);
    } else {
        snprintf(search, sizeof(search), "%s\\*", path);
    }

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(search, &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        // Skip special entries and system files
        if (strcmp(fd.cFileName, ".") == 0 || 
            strcmp(fd.cFileName, "..") == 0 ||
            (fd.dwFileAttributes & (FILE_ATTRIBUTE_SYSTEM | 
                                   FILE_ATTRIBUTE_HIDDEN | 
                                   FILE_ATTRIBUTE_TEMPORARY))) {
            continue;
        }

        char full[MAX_PATH_BUFFER];
        path_len = strlen(path);
        
        // Build full path with proper slash handling
        if (path_len > 0 && (path[path_len - 1] == '\\' || path[path_len - 1] == '/')) {
            snprintf(full, sizeof(full), "%s%s", path, fd.cFileName);
        } else {
            snprintf(full, sizeof(full), "%s\\%s", path, fd.cFileName);
        }

        // Skip reparse points (like symlinks)
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            continue;
        }

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            process_win(full, list);  // Recurse into directories
        } else {
            // Add file with its size to the list
            uint64_t size = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            file_list_add(list, full, size);
        }
    } while (FindNextFileA(h, &fd));
    
    FindClose(h);
}

int main() {
    init_console();
    printf("File Scanner\n");
    printf("----------------------------------------\n");

    char **drives = NULL;
    size_t drive_count = 0;
    if (!enumerate_drives(&drives, &drive_count)) {
        fprintf(stderr, "Drive enumeration failed!\n");
        return 1;
    }

    const char *outfile = "largest_files.txt";
    FILE *fp = fopen(outfile, "w");
    if (fp) fclose(fp);  // Create/truncate output file

    for (size_t i = 0; i < drive_count; i++) {
        printf("\nProcessing %s\n", drives[i]);
        FileList files;
        file_list_init(&files);

        DWORD start = GetTickCount();
        process_win(drives[i], &files);
        DWORD duration = GetTickCount() - start;

        printf("Scanned %s in %.1f seconds\n", drives[i], duration/1000.0);
        printf("Found %zu files\n", files.size);

        if (files.size == 0) {
            file_list_free(&files);
            continue;
        }

        qsort(files.entries, files.size, sizeof(FileEntry), compare_entries);

        FILE *fp = fopen(outfile, "a");
        if (fp) {
            // Display drive as "C:" instead of "C:\\"
            char drive_display[3] = {drives[i][0], ':', '\0'};
            fprintf(fp, "Largest files on %s:\n", drive_display);
            
            size_t limit = files.size < 100 ? files.size : 100;
            for (size_t j = 0; j < limit; j++) {
                // Convert size to MB and write with cleaned path
                double mb = files.entries[j].size / (1024.0 * 1024.0);
                fprintf(fp, "%s: %.2f MB\n", files.entries[j].path, mb);
            }
            fprintf(fp, "\n");
            fclose(fp);
        }
        file_list_free(&files);
    }

    // Cleanup drive list
    for (size_t i = 0; i < drive_count; i++) free(drives[i]);
    free(drives);
    
    printf("\nScan complete. Results saved to %s\n", outfile);
    printf("Press Enter to exit...");
    getchar();

    return 0;
}