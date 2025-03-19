// File: Program.fs
open System
open System.IO

type FileEntry =
    { Path : string
      Size : int64 }

/// Recursively collects all files under `startPath`,
/// skipping those marked system/hidden/temporary/reparse.
let rec gatherFiles (startPath: string) =
    let mutable results = []
    try
        // For each file in the current directory
        let files = Directory.GetFiles(startPath)
        for file in files do
            try
                let attrs = File.GetAttributes(file)
                // Skip system/hidden/temp/reparse
                if (attrs.HasFlag(FileAttributes.Hidden)    ||
                    attrs.HasFlag(FileAttributes.System)    ||
                    attrs.HasFlag(FileAttributes.Temporary) ||
                    attrs.HasFlag(FileAttributes.ReparsePoint)) then
                    ()
                else
                    let size = FileInfo(file).Length
                    results <- { Path = file; Size = size } :: results
            with _ -> ()
        
        // Then recurse into subdirectories
        let dirs = Directory.GetDirectories(startPath)
        for dir in dirs do
            try
                let dirAttrs = File.GetAttributes(dir)
                if (dirAttrs.HasFlag(FileAttributes.Hidden)    ||
                    dirAttrs.HasFlag(FileAttributes.System)    ||
                    dirAttrs.HasFlag(FileAttributes.Temporary) ||
                    dirAttrs.HasFlag(FileAttributes.ReparsePoint)) then
                    ()
                else
                    results <- gatherFiles dir @ results
            with _ -> ()
    with _ -> ()
    results

[<EntryPoint>]
let main _ =
    // Only run on Windows
    if not (Environment.OSVersion.Platform = PlatformID.Win32NT) then
        printfn "This program requires Windows."
        0
    else
        // Print top lines
        printfn "File Scanner"
        printfn "----------------------------------------"

        // Print [Drive Scan], listing which drives we're including
        printfn "[Drive Scan]"
        let drivesAll = DriveInfo.GetDrives()
        let drives =
            drivesAll
            |> Array.filter (fun d ->
                d.DriveType = DriveType.Fixed ||
                d.DriveType = DriveType.Removable)
        
        for d in drivesAll do
            // Show all, but only say "Including drive:" if it meets our condition
            if drives |> Array.exists (fun x -> x.Name = d.Name) then
                printfn "Including drive: %s" d.Name
            else
                printfn "Skipping drive:  %s" d.Name

        if drives.Length = 0 then
            eprintfn "No suitable drives found!"
            0
        else
            let outFile = "largest_files.txt"
            File.WriteAllText(outFile, "")  // Clear/overwrite existing

            for drive in drives do
                try
                    printfn "\nProcessing %s" drive.Name
                    let sw = Diagnostics.Stopwatch.StartNew()

                    let allFiles = gatherFiles drive.Name
                    sw.Stop()

                    printfn "Scanned %s in %.1f seconds" drive.Name sw.Elapsed.TotalSeconds
                    printfn "Found %d files" allFiles.Length

                    if allFiles.Length > 0 then
                        // Sort descending by size
                        let sorted = allFiles |> List.sortByDescending (fun f -> f.Size)

                        // Take top 100
                        let top = sorted |> List.truncate 100
                        use writer = File.AppendText(outFile)

                        // e.g. "C:" instead of "C:\"
                        let driveDisplay = drive.Name.Substring(0, 2)
                        writer.WriteLine(sprintf "Largest files on %s:" driveDisplay)
                        for f in top do
                            let mb = float f.Size / (1024.0 * 1024.0)
                            writer.WriteLine(sprintf "%s: %.2f MB" f.Path mb)
                        writer.WriteLine()
                with ex ->
                    printfn "Error processing %s: %s" drive.Name ex.Message

            printfn "\nScan complete. Results saved to %s" outFile
            printfn "Press Enter to exit..."
            Console.ReadLine() |> ignore
            0
