## Compilation

### Requirements
- GCC (MinGW-w64 recommended on Windows)
- Windows SDK headers

### Build Command
```bash
gcc -o LargestFiles LargestFiles.c -O2 -Wall -lshlwapi
```

### Compilation Flags Explained
- `-O2`: Optimize for performance
- `-Wall`: Enable all warnings
- `-lshlwapi`: Link Windows Shell API library

### Running
```bash
./LargestFiles
```

### Clean Build
```bash
del LargestFiles.exe  # Windows
rm -f LargestFiles    # Linux/WSL
```

## Features
- Pure C implementation
- Manual memory management
- Recursive directory traversal
- Outputs to `largest_files.txt`