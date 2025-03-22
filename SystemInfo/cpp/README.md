# SystemInfo Compilation Instructions

This README provides instructions to compile the `SystemInfo.cpp` file into an executable (`SystemInfo.exe`) using MinGW (g++).

## Prerequisites

- **MinGW (g++)**: Ensure MinGW is installed on your system. You can download it from [MinGW-w64](https://www.mingw-w64.org/).
- **PATH Configuration**: The MinGW `bin` directory (e.g., `C:\MinGW\bin`) must be added to your system's PATH environment variable so you can run `g++` from the command line. To check or configure this:
  1. Right-click 'This PC' > 'Properties' > 'Advanced system settings'.
  2. Click 'Environment Variables'.
  3. Under 'System variables', edit 'Path' and add the MinGW `bin` directory if it’s not already present.
  4. Click 'OK' to save.

## Compilation Command

To compile `SystemInfo.cpp` into `SystemInfo.exe`, open a command prompt in the directory containing `SystemInfo.cpp` and run:

```bash
g++ -o SystemInfo.exe SystemInfo.cpp -std=c++17 -static-libgcc -static-libstdc++ -loleaut32 -lole32 -lwbemuuid -lshlwapi -O2
```

### Command Breakdown

- **`g++`**: The C++ compiler from MinGW.
- **`-o SystemInfo.exe`**: Sets the output executable name to `SystemInfo.exe`.
- **`SystemInfo.cpp`**: The source file being compiled.
- **`-std=c++17`**: Uses the C++17 standard, required by the program.
- **`-static-libgcc -static-libstdc++`**: Links the GCC and C++ standard libraries statically, avoiding external DLL dependencies.
- **`-loleaut32 -lole32 -lwbemuuid -lshlwapi`**: Links Windows libraries needed for the program:
  - `oleaut32`: OLE Automation.
  - `ole32`: OLE32 library.
  - `wbemuuid`: WMI library.
  - `shlwapi`: Shell Lightweight Utility APIs.
- **`-O2`**: Optimizes the code for better performance.

## Troubleshooting

- If `g++` is not recognized, verify that MinGW is installed and its `bin` directory is in your PATH.
- Ensure you run the command in the directory containing `SystemInfo.cpp`.

After compilation, `SystemInfo.exe` will be created in the same directory.
