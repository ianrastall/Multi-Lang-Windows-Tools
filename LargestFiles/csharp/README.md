## Compilation Instructions

This repository contains the source code for the Largest Files Finder project, along with a precompiled standalone executable.

### Prerequisites

- **.NET 9.0 SDK or later**  
  [Download .NET 9.0 SDK](https://dotnet.microsoft.com/en-us/download/dotnet/9.0)
- **Windows OS** (The project is designed for Windows)

### Files Included

- `LargestFiles.csproj` — Project file with build configuration.
- `LargestFiles.cs` — C# source code.
- `LargestFiles.exe` — Precompiled standalone executable.

### Compiling from Source

To compile and publish the project as a standalone EXE, follow these steps:

1. Open a command prompt or terminal in the directory containing these files.
2. Run the following command:

   ```bash
   dotnet publish -c Release -r win-x64 --self-contained true /p:PublishSingleFile=true
