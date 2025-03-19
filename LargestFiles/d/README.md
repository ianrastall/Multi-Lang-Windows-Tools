## Prerequisites

- **Operating System:** Windows (the program uses Windows-specific API calls)
- **D Compiler:**  
  - [DMD](https://dlang.org/download.html) (Digital Mars D Compiler) or  
  - [LDC](https://github.com/ldc-developers/ldc) (LLVM-based D Compiler)
- Make sure the compiler's `bin` directory is in your system PATH.

## Compilation Instructions

### Using DMD

1. **Open Command Prompt:**  
   Navigate to the directory containing `LargestFiles.d`.

2. **Compile in Release Mode:**  
   Run the following command to compile with optimizations:
   ```sh
   dmd -O -release LargestFiles.d
