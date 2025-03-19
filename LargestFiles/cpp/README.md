## Compilation

### Requirements
- G++ (MinGW-w64 recommended on Windows)
- C++17 compatible compiler

### Build Command
```bash
g++ -o LargestFilesCpp LargestFiles.cpp -O2 -Wall -static -lshlwapi
```

### Flags Breakdown
- `-O2`: Performance optimization
- `-Wall`: Show all warnings
- `-static`: Static linking for better portability
- `-lshlwapi`: Windows Shell API linkage

### Execution
```bash
./LargestFilesCpp
```

### Clean
```bash
del LargestFilesCpp.exe  # Windows
rm -f LargestFilesCpp    # Linux/WSL
```

## Key Differences from C Version
- RAII for automatic resource management
- STL containers (`std::vector`)
- Lambda functions for sorting
- Stream-based file output