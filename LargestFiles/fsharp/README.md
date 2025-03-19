## Requirements
- .NET SDK 9.0 or later
- Windows OS (program is Windows-specific)
- F# support (included with .NET SDK)

## Compilation

### Using the provided project file
1. Save the provided code as:
   - `Program.fs`
   - `LargestFiles.fsproj` (the XML project file)

2. Run this command from the project directory:
```bash
dotnet publish -c Release -r win-x64 --self-contained true /p:PublishSingleFile=true
```

### Command Breakdown
- `-c Release`: Build in Release configuration
- `-r win-x64`: Target 64-bit Windows
- `--self-contained true`: Include .NET runtime in output
- `/p:PublishSingleFile=true`: Create single executable
- Output will be in `bin\Release\net9.0\win-x64\publish\`

### Alternative Commands
For a debug build:
```bash
dotnet build
```

For cross-platform build (note: program still requires Windows):
```bash
dotnet publish -c Release --self-contained true /p:PublishSingleFile=true
```

## Usage
1. Run the compiled executable (`LargestFiles.exe`)
2. The program will:
   - Scan all fixed and removable drives
   - Skip system/hidden/temporary files
   - Create/overwrite `largest_files.txt` with results
3. Results include:
   - Full file paths
   - Sizes in MB
   - Top 100 files per drive

Example output location:
```
largest_files.txt
```

## Notes
- The self-contained executable is larger (~60-150MB) but requires no .NET runtime
- Program will pause with "Press Enter to exit" when complete
- Drives are scanned sequentially
- Errors for individual directories/files will be reported but won't stop execution

## Customization
You can modify the `.fsproj` file to:
- Change target framework (net8.0, net7.0, etc.)
- Adjust runtime identifier (win-x86, linux-x64, etc.)
- Disable single-file publishing
- Modify output type