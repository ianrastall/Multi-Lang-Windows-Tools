using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;

class LargestFilesFinder
{
    private const int MAX_PATH = 260;
    private const int MAX_ALTERNATE = 14;

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Auto)]
    private struct WIN32_FIND_DATA
    {
        public FileAttributes dwFileAttributes;
        public System.Runtime.InteropServices.ComTypes.FILETIME ftCreationTime;
        public System.Runtime.InteropServices.ComTypes.FILETIME ftLastAccessTime;
        public System.Runtime.InteropServices.ComTypes.FILETIME ftLastWriteTime;
        public uint nFileSizeHigh;
        public uint nFileSizeLow;
        public uint dwReserved0;
        public uint dwReserved1;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = MAX_PATH)]
        public string cFileName;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = MAX_ALTERNATE)]
        public string cAlternate;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Auto)]
    private static extern IntPtr FindFirstFile(string lpFileName, out WIN32_FIND_DATA lpFindFileData);

    [DllImport("kernel32.dll", CharSet = CharSet.Auto)]
    private static extern bool FindNextFile(IntPtr hFindFile, out WIN32_FIND_DATA lpFindFileData);

    [DllImport("kernel32.dll")]
    private static extern bool FindClose(IntPtr hFindFile);

    [DllImport("kernel32.dll")]
    private static extern uint GetLogicalDrives();

    [DllImport("kernel32.dll", CharSet = CharSet.Auto)]
    private static extern uint GetDriveType(string lpRootPathName);

    static void Main()
    {
        if (!RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
        {
            Console.WriteLine("This program requires Windows");
            return;
        }

        var drives = new List<string>();
        uint mask = GetLogicalDrives();
        
        Console.WriteLine("[Drive Scan]");
        for (char c = 'A'; c <= 'Z'; c++)
        {
            if ((mask & (1 << (c - 'A'))) != 0)
            {
                string drive = $"{c}:\\";
                uint type = GetDriveType(drive);
                
                if (type == 3 || type == 2) // DRIVE_FIXED (3) or DRIVE_REMOVABLE (2)
                {
                    Console.WriteLine($"Including drive: {drive}");
                    drives.Add(drive);
                }
            }
        }

        const string outfile = "largest_files.txt";
        File.WriteAllText(outfile, string.Empty);

        foreach (var drive in drives)
        {
            Console.WriteLine($"\nProcessing {drive}");
            var sw = Stopwatch.StartNew();
            var files = new List<FileEntry>();
            
            try
            {
                ProcessDirectory(drive, files);
                files.Sort((a, b) => b.Size.CompareTo(a.Size));
                
                Console.WriteLine($"Scanned {drive} in {sw.Elapsed.TotalSeconds:F1} seconds");
                Console.WriteLine($"Found {files.Count} files");

                using var writer = File.AppendText(outfile);
                writer.WriteLine($"Largest files on {drive.Substring(0, 2)}:");
                foreach (var file in files.Take(100))
                {
                    writer.WriteLine($"{file.Path}: {file.Size / (1024.0 * 1024.0):F2} MB");
                }
                writer.WriteLine();
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error processing {drive}: {ex.Message}");
            }
        }

        Console.WriteLine("\nScan complete. Results saved to largest_files.txt");
        Console.WriteLine("Press Enter to exit...");
        Console.ReadLine();
    }

    private class FileEntry
    {
        public string Path { get; }
        public ulong Size { get; }

        public FileEntry(string path, ulong size)
        {
            // Normalize path separators to single backslash
            Path = path.Replace("\\\\", "\\");
            Size = size;
        }
    }

    private static void ProcessDirectory(string path, List<FileEntry> files)
    {
        var findData = new WIN32_FIND_DATA();
        IntPtr findHandle = FindFirstFile(System.IO.Path.Combine(path, "*"), out findData);

        if (findHandle == IntPtr.Zero) return;

        do
        {
            if (findData.cFileName == "." || findData.cFileName == "..") continue;
            if ((findData.dwFileAttributes & (FileAttributes.System | 
                                            FileAttributes.Hidden | 
                                            FileAttributes.Temporary)) != 0) continue;
            if ((findData.dwFileAttributes & FileAttributes.ReparsePoint) != 0) continue;

            string fullPath = System.IO.Path.Combine(path, findData.cFileName);

            if ((findData.dwFileAttributes & FileAttributes.Directory) != 0)
            {
                ProcessDirectory(fullPath, files);
            }
            else
            {
                ulong size = ((ulong)findData.nFileSizeHigh << 32) | findData.nFileSizeLow;
                files.Add(new FileEntry(fullPath, size));
            }
        } 
        while (FindNextFile(findHandle, out findData));

        FindClose(findHandle);
    }
}
