#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <tchar.h>
#include <string.h>
#include <ctype.h>

typedef struct _FileEntry {
    TCHAR *path;
    LARGE_INTEGER fileSize;
    BYTE *hash;
    struct _FileEntry *next;
} FileEntry;

// Function prototypes
void TraverseDirectory(LPCTSTR dirPath, BOOL recursive, FileEntry **fileList);
BOOL ComputeFileHash(LPCTSTR filePath, BYTE **hash, DWORD *hashSize);
void ComputeHashes(FileEntry *group);
int CompareFileSizes(const void *a, const void *b);
int CompareHashes(const void *a, const void *b);
void GroupBySize(FileEntry **sortedFiles, int count, FileEntry ***groups, int *numGroups);
FileEntry **SortFilesBySize(FileEntry *list, int *count);
FileEntry **GroupByHash(FileEntry *group, int *numGroups);
void HandleDuplicateGroup(FileEntry *group);
void FreeFileList(FileEntry *list);
BOOL IsSymbolicLink(LPCTSTR path);
int CountFiles(FileEntry *list);

// New helper prototypes
LPCTSTR GetFileName(LPCTSTR path);
BOOL IsOnlyDigitsAndPunctuation(LPCTSTR str);
BOOL AreFilenamesSimilar(LPCTSTR name1, LPCTSTR name2);

int _tmain(int argc, TCHAR *argv[]) {
    TCHAR directory[MAX_PATH] = {0};
    BOOL recursive = FALSE;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (_tcscmp(argv[i], _T("-r")) == 0) {
            recursive = TRUE;
        } else if (directory[0] == 0) {
            _tcscpy_s(directory, MAX_PATH, argv[i]);
        }
    }

    if (directory[0] == 0) {
        GetCurrentDirectory(MAX_PATH, directory);
    }

    FileEntry *fileList = NULL;
    TraverseDirectory(directory, recursive, &fileList);

    int fileCount = 0;
    FileEntry **sortedFiles = SortFilesBySize(fileList, &fileCount);

    FileEntry **sizeGroups = NULL;
    int numSizeGroups = 0;
    GroupBySize(sortedFiles, fileCount, &sizeGroups, &numSizeGroups);
    free(sortedFiles);

    for (int i = 0; i < numSizeGroups; i++) {
        FileEntry *group = sizeGroups[i];
        if (CountFiles(group) > 1) {
            ComputeHashes(group);
            
            int numHashGroups = 0;
            FileEntry **hashGroups = GroupByHash(group, &numHashGroups);
            
            for (int j = 0; j < numHashGroups; j++) {
                if (CountFiles(hashGroups[j]) > 1) {
                    HandleDuplicateGroup(hashGroups[j]);
                }
                // Removed freeing per DeepSeek suggestion.
            }
            free(hashGroups);
        }
        // Removed freeing per DeepSeek suggestion.
    }
    free(sizeGroups);

    FreeFileList(fileList);
    return 0;
}

void TraverseDirectory(LPCTSTR dirPath, BOOL recursive, FileEntry **fileList) {
    WIN32_FIND_DATA findFileData;
    HANDLE hFind = INVALID_HANDLE_VALUE;
    TCHAR searchPath[MAX_PATH];

    _stprintf_s(searchPath, MAX_PATH, _T("%s\\*"), dirPath);

    hFind = FindFirstFile(searchPath, &findFileData);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (_tcscmp(findFileData.cFileName, _T(".")) == 0 ||
            _tcscmp(findFileData.cFileName, _T("..")) == 0) {
            continue;
        }

        TCHAR fullPath[MAX_PATH];
        _stprintf_s(fullPath, MAX_PATH, _T("%s\\%s"), dirPath, findFileData.cFileName);

        if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (recursive) {
                TraverseDirectory(fullPath, recursive, fileList);
            }
        } else {
            FileEntry *entry = (FileEntry *)malloc(sizeof(FileEntry));
            entry->path = _tcsdup(fullPath);
            entry->fileSize.LowPart = findFileData.nFileSizeLow;
            entry->fileSize.HighPart = findFileData.nFileSizeHigh;
            entry->hash = NULL;
            entry->next = *fileList;
            *fileList = entry;
        }
    } while (FindNextFile(hFind, &findFileData));

    FindClose(hFind);
}

BOOL ComputeFileHash(LPCTSTR filePath, BYTE **hash, DWORD *hashSize) {
    HANDLE hFile = CreateFile(filePath, GENERIC_READ, FILE_SHARE_READ, 
                            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;

    if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        CloseHandle(hFile);
        return FALSE;
    }

    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        CloseHandle(hFile);
        return FALSE;
    }

    BYTE buffer[4096];
    DWORD bytesRead;
    while (ReadFile(hFile, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0) {
        if (!CryptHashData(hHash, buffer, bytesRead, 0)) {
            CryptDestroyHash(hHash);
            CryptReleaseContext(hProv, 0);
            CloseHandle(hFile);
            return FALSE;
        }
    }

    DWORD len = 0;
    DWORD dummy = sizeof(DWORD);
    if (!CryptGetHashParam(hHash, HP_HASHSIZE, (BYTE *)&len, &dummy, 0)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        CloseHandle(hFile);
        return FALSE;
    }

    *hash = (BYTE *)malloc(len);
    *hashSize = len;

    if (!CryptGetHashParam(hHash, HP_HASHVAL, *hash, &len, 0)) {
        free(*hash);
        *hash = NULL;
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        CloseHandle(hFile);
        return FALSE;
    }

    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    CloseHandle(hFile);
    return TRUE;
}

void ComputeHashes(FileEntry *group) {
    FileEntry *current = group;
    DWORD hashSize;
    while (current) {
        if (!ComputeFileHash(current->path, &current->hash, &hashSize)) {
            _tprintf(_T("Error computing hash for file: %s\n"), current->path);
        }
        current = current->next;
    }
}

FileEntry **SortFilesBySize(FileEntry *list, int *count) {
    int n = 0;
    FileEntry *current = list;
    while (current) { n++; current = current->next; }

    FileEntry **array = (FileEntry **)malloc(n * sizeof(FileEntry *));
    current = list;
    for (int i = 0; i < n; i++) {
        array[i] = current;
        current = current->next;
    }

    qsort(array, n, sizeof(FileEntry *), CompareFileSizes);
    *count = n;
    return array;
}

int CompareFileSizes(const void *a, const void *b) {
    FileEntry *ea = *(FileEntry **)a;
    FileEntry *eb = *(FileEntry **)b;
    return (ea->fileSize.QuadPart > eb->fileSize.QuadPart) ? 1 : 
          ((ea->fileSize.QuadPart < eb->fileSize.QuadPart) ? -1 : 0);
}

void GroupBySize(FileEntry **sortedFiles, int count, FileEntry ***groups, int *numGroups) {
    if (count == 0) return;

    int groupCount = 1;
    LONGLONG prevSize = sortedFiles[0]->fileSize.QuadPart;

    for (int i = 1; i < count; i++) {
        if (sortedFiles[i]->fileSize.QuadPart != prevSize) {
            groupCount++;
            prevSize = sortedFiles[i]->fileSize.QuadPart;
        }
    }

    *groups = (FileEntry **)malloc(groupCount * sizeof(FileEntry *));
    *numGroups = groupCount;

    int groupIndex = 0;
    FileEntry *currentGroup = NULL;
    prevSize = sortedFiles[0]->fileSize.QuadPart;

    for (int i = 0; i < count; i++) {
        if (sortedFiles[i]->fileSize.QuadPart != prevSize) {
            (*groups)[groupIndex++] = currentGroup;
            currentGroup = NULL;
            prevSize = sortedFiles[i]->fileSize.QuadPart;
        }
        sortedFiles[i]->next = currentGroup;
        currentGroup = sortedFiles[i];
    }
    (*groups)[groupIndex] = currentGroup;
}

int CompareHashes(const void *a, const void *b) {
    FileEntry *ea = *(FileEntry **)a;
    FileEntry *eb = *(FileEntry **)b;
    if (ea->hash == NULL && eb->hash == NULL) return 0;
    if (ea->hash == NULL) return 1;
    if (eb->hash == NULL) return -1;
    return memcmp(ea->hash, eb->hash, 32);
}

FileEntry **GroupByHash(FileEntry *group, int *numGroups) {
    int count = 0;
    FileEntry *current = group;
    while (current) { count++; current = current->next; }

    FileEntry **array = (FileEntry **)malloc(count * sizeof(FileEntry *));
    current = group;
    for (int i = 0; i < count; i++) {
        array[i] = current;
        current = current->next;
    }

    qsort(array, count, sizeof(FileEntry *), CompareHashes);

    int groupCount = 1;
    BYTE *prevHash = array[0]->hash;
    for (int i = 1; i < count; i++) {
        if (memcmp(array[i]->hash, prevHash, 32) != 0) {
            groupCount++;
            prevHash = array[i]->hash;
        }
    }

    FileEntry **groups = (FileEntry **)malloc(groupCount * sizeof(FileEntry *));
    *numGroups = groupCount;

    int groupIndex = 0;
    FileEntry *currentGroup = NULL;
    prevHash = array[0]->hash;

    for (int i = 0; i < count; i++) {
        if (memcmp(array[i]->hash, prevHash, 32) != 0) {
            groups[groupIndex++] = currentGroup;
            currentGroup = NULL;
            prevHash = array[i]->hash;
        }
        array[i]->next = currentGroup;
        currentGroup = array[i];
    }
    groups[groupIndex] = currentGroup;
    free(array);
    return groups;
}

// New: Extracts file name from full path.
LPCTSTR GetFileName(LPCTSTR path) {
    LPCTSTR p = _tcsrchr(path, _T('\\'));
    return p ? p + 1 : path;
}

// New: Checks if string contains only digits and selected punctuation.
BOOL IsOnlyDigitsAndPunctuation(LPCTSTR str) {
    while (*str) {
        if (!_istdigit(*str) && !_tcschr(_T("()[]{} "), *str))
            return FALSE;
        str++;
    }
    return TRUE;
}

// New: Compares file names (without path and extension) for similarity.
BOOL AreFilenamesSimilar(LPCTSTR name1, LPCTSTR name2) {
    // Extract base names.
    TCHAR base1[MAX_PATH], base2[MAX_PATH];
    _tcscpy_s(base1, MAX_PATH, GetFileName(name1));
    _tcscpy_s(base2, MAX_PATH, GetFileName(name2));
    // Remove extension.
    TCHAR *dot = _tcsrchr(base1, _T('.'));
    if (dot) *dot = _T('\0');
    dot = _tcsrchr(base2, _T('.'));
    if (dot) *dot = _T('\0');
    // If exactly equal (case-insensitive), they are similar.
    if (_tcsicmp(base1, base2) == 0) return TRUE;
    // Check if one is a prefix of the other.
    size_t len1 = _tcslen(base1), len2 = _tcslen(base2);
    if (len1 == 0 || len2 == 0) return FALSE;
    if (len1 < len2) {
        if (_tcsnicmp(base1, base2, len1) == 0)
            return IsOnlyDigitsAndPunctuation(base2 + len1);
    } else if (len2 < len1) {
        if (_tcsnicmp(base2, base1, len2) == 0)
            return IsOnlyDigitsAndPunctuation(base1 + len2);
    }
    return FALSE;
}

void HandleDuplicateGroup(FileEntry *group) {
    int count = CountFiles(group);
    if (count < 2) return;

    // Check if file names are similar.
    FileEntry *iter = group;
    LPCTSTR refName = GetFileName(iter->path);
    BOOL namesSimilar = TRUE;
    while (iter) {
        if (!AreFilenamesSimilar(refName, GetFileName(iter->path))) {
            namesSimilar = FALSE;
            break;
        }
        iter = iter->next;
    }
    if (!namesSimilar) {
        _tprintf(_T("Skipping group with dissimilar file names.\n"));
        return;
    }

    _tprintf(_T("\nFound %d duplicate files:\n"), count);
    FileEntry *current = group;
    int index = 1;
    while (current) {
        _tprintf(_T("%d) %s\n"), index++, current->path);
        current = current->next;
    }

    TCHAR input[256];
    _tprintf(_T("Enter files to keep (comma-separated), 's' to skip, 'q' to quit: "));
    if (!_fgetts(input, 256, stdin)) return;

    input[_tcslen(input) - 1] = 0; // Remove newline

    if (_tcscmp(input, _T("s")) == 0) return;
    if (_tcscmp(input, _T("q")) == 0) exit(0);

    int *keep = NULL;
    int keepCount = 0;
    TCHAR *token = _tcstok(input, _T(","));
    while (token) {
        int num = _ttoi(token);
        if (num >= 1 && num <= count) {
            keep = (int *)realloc(keep, (keepCount + 1) * sizeof(int));
            keep[keepCount++] = num;
        }
        token = _tcstok(NULL, _T(","));
    }

    if (keepCount == 0) {
        _tprintf(_T("No valid files selected.\n"));
        free(keep);
        return;
    }

    FileEntry *toDelete = NULL;
    current = group;
    index = 1;
    while (current) {
        BOOL found = FALSE;
        for (int i = 0; i < keepCount; i++) {
            if (index == keep[i]) {
                found = TRUE;
                break;
            }
        }
        if (!found) {
            FileEntry *entry = (FileEntry *)malloc(sizeof(FileEntry));
            entry->path = _tcsdup(current->path);
            entry->next = toDelete;
            toDelete = entry;
        }
        current = current->next;
        index++;
    }
    free(keep);

    _tprintf(_T("The following files will be deleted:\n"));
    current = toDelete;
    while (current) {
        _tprintf(_T("%s\n"), current->path);
        current = current->next;
    }

    _tprintf(_T("Confirm deletion (y/n)? "));
    if (!_fgetts(input, 256, stdin)) {
        FreeFileList(toDelete);
        return;
    }

    if (_totlower(input[0]) != _T('y')) {
        _tprintf(_T("Deletion cancelled.\n"));
        FreeFileList(toDelete);
        return;
    }

    current = toDelete;
    while (current) {
        if (!IsSymbolicLink(current->path)) {
            if (!DeleteFile(current->path)) {
                _tprintf(_T("Error deleting %s (%lu)\n"), 
                        current->path, GetLastError());
            } else {
                _tprintf(_T("Deleted: %s\n"), current->path);
            }
        } else {
            _tprintf(_T("Skipped symbolic link: %s\n"), current->path);
        }
        FileEntry *next = current->next;
        free(current->path);
        free(current);
        current = next;
    }
}

BOOL IsSymbolicLink(LPCTSTR path) {
    DWORD attrs = GetFileAttributes(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) return FALSE;
    
    if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
        WIN32_FIND_DATA data;
        HANDLE hFind = FindFirstFile(path, &data);
        if (hFind != INVALID_HANDLE_VALUE) {
            FindClose(hFind);
            return data.dwReserved0 == IO_REPARSE_TAG_SYMLINK;
        }
    }
    return FALSE;
}

int CountFiles(FileEntry *list) {
    int count = 0;
    while (list) { count++; list = list->next; }
    return count;
}

void FreeFileList(FileEntry *list) {
    while (list) {
        FileEntry *next = list->next;
        free(list->path);
        free(list->hash);
        free(list);
        list = next;
    }
}
