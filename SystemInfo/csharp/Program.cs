#nullable enable
using System;
using System.Collections.Generic;
using System.Management; // Requires reference to System.Management.dll
using System.Text;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Diagnostics;
using System.Runtime.Versioning;

[SupportedOSPlatform("windows")]
class Program
{
    static void Main()
    {
        if (!RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
            return;

        StringBuilder output = new StringBuilder();
        
        PrintSystemSummary(output);
        PrintHardwareResources(output);
        PrintComponents(output);
        PrintSoftwareEnvironment(output);
        PrintLocaleAndEncodingInfo(output);
        PrintInstalledProgrammingLanguages(output);

        File.WriteAllText("system_info.txt", output.ToString());
    }

    static void PrintSystemSummary(StringBuilder sb)
    {
        PrintSectionHeader(sb, "System Summary");
        
        QueryWMI(sb, "Win32_OperatingSystem", new List<WmiProperty> {
            new("Caption", "OS Name"),
            new("Version", "Version"),
            new("BuildNumber", "Build"),
            new("OSArchitecture", "Architecture"),
            new("SerialNumber", "Serial"),
            new("InstallDate", "Install Date")
        });

        QueryWMI(sb, "Win32_BIOS", new List<WmiProperty> {
            new("Manufacturer", "BIOS Vendor"),
            new("Name", "BIOS Version"),
            new("ReleaseDate", "Release Date"),
            new("SMBIOSBIOSVersion", "SMBIOS Version")
        });

        QueryWMI(sb, "Win32_ComputerSystem", new List<WmiProperty> {
            new("Manufacturer", "System Manufacturer"),
            new("Model", "System Model"),
            new("SystemType", "System Type"),
            // Use null-forgiving operator (!) to silence CS8600 warning.
            new("TotalPhysicalMemory", "Total Physical Memory (GB)", v => $"{Convert.ToUInt64(v!) / (1024 * 1024 * 1024):N1}")
        });
    }

    static void PrintHardwareResources(StringBuilder sb)
    {
        PrintSectionHeader(sb, "Hardware Resources");
        
        QueryWMI(sb, "Win32_PhysicalMemory", new List<WmiProperty> {
            new("Capacity", "Memory Capacity (GB)", v => $"{Convert.ToUInt64(v!) / (1024 * 1024 * 1024):N1}"),
            new("Speed", "Speed (MHz)"),
            new("Manufacturer", "Manufacturer")
        }, "Memory Devices");

        QueryWMI(sb, "Win32_Processor", new List<WmiProperty> {
            new("Name", "Processor"),
            new("NumberOfCores", "Cores"),
            new("NumberOfLogicalProcessors", "Logical Processors"),
            new("MaxClockSpeed", "Max Speed (MHz)"),
            new("L2CacheSize", "L2 Cache (MB)", v => $"{Convert.ToUInt64(v!) / 1024:N1}"),
            new("L3CacheSize", "L3 Cache (MB)", v => $"{Convert.ToUInt64(v!) / 1024:N1}")
        }, "Processor Details");
    }

    static void PrintComponents(StringBuilder sb)
    {
        PrintSectionHeader(sb, "Components");
        
        QueryWMI(sb, "Win32_VideoController", new List<WmiProperty> {
            new("Name", "Adapter"),
            new("AdapterRAM", "VRAM (GB)", v => $"{Convert.ToUInt64(v!) / (1024 * 1024 * 1024):N1}"),
            new("DriverVersion", "Driver Version"),
            new("VideoProcessor", "GPU Chip")
        }, "Display");

        QueryWMI(sb, "Win32_DiskDrive", new List<WmiProperty> {
            new("Model", "Disk Model"),
            new("Size", "Capacity (GB)", v => $"{Convert.ToUInt64(v!) / (1024 * 1024 * 1024):N1}"),
            new("InterfaceType", "Interface")
        }, "Storage");
    }

    static void PrintSoftwareEnvironment(StringBuilder sb)
    {
        PrintSectionHeader(sb, "Software Environment");
        
        QueryWMI(sb, "Win32_QuickFixEngineering", new List<WmiProperty> {
            new("HotFixID", "Update"),
            new("InstalledOn", "Install Date"),
            new("Description", "Description")
        }, "Windows Updates");

        QueryWMI(sb, "Win32_NetworkAdapterConfiguration", new List<WmiProperty> {
            new("Description", "Adapter"),
            new("IPAddress", "IP Address"),
            new("MACAddress", "MAC")
        }, "Network", "IPEnabled = TRUE");
    }

    static void PrintLocaleAndEncodingInfo(StringBuilder sb)
    {
        PrintSectionHeader(sb, "Locale and Encoding");
        sb.AppendLine($"System Locale: {CultureInfo.CurrentCulture.Name}");
        sb.AppendLine($"Default Encoding: {Encoding.Default.EncodingName}");
    }

    static void PrintInstalledProgrammingLanguages(StringBuilder sb)
    {
        PrintSectionHeader(sb, "Installed Programming Languages");

        var languages = new (string Name, string[] Candidates)[]
        {
            ("C", new []{"cl.exe", "gcc.exe", "clang.exe"}),
            ("C++", new []{"cl.exe", "g++.exe", "clang++.exe"}),
            ("C#", new []{"csc.exe", "dotnet.exe"}),
            ("D", new []{"dmd.exe", "ldc2.exe", "gdc.exe"}),
            ("Java", new []{"java.exe", "javac.exe"}),
            ("Kotlin", new []{"kotlinc.exe", "kotlinc-jvm.exe", "kotlin.bat"}),
            ("Scala", new []{"scala.exe", "scalac.exe"}),
            ("Go", new []{"go.exe"}),
            ("Rust", new []{"rustc.exe", "cargo.exe"}),
            ("Swift", new []{"swift.exe", "swiftc.exe"}),
            ("F#", new []{"fsc.exe", "fsi.exe", "dotnet.exe"}),
            ("Fortran", new []{"gfortran.exe", "ifort.exe"}),
            ("Pascal", new []{"fpc.exe", "ppc386.exe", "ppcx64.exe"}),
            ("Delphi", new []{"dcc32.exe", "dcc64.exe", "bds.exe"}),
            ("Ada", new []{"gnat.exe", "gcc.exe"}),
            ("Objective-C", new []{"gcc.exe", "clang.exe"}),
            ("Zig", new [] {"zig.exe"}),
            ("Nim", new [] {"nim.exe", "nimble.exe"}),

            ("Python", new []{"python.exe", "python3.exe", "pypy.exe", "pypy3.exe"}),
            ("Perl", new []{"perl.exe"}),
            ("PHP", new []{"php.exe", "php-cgi.exe"}),
            ("Ruby", new []{"ruby.exe", "irb.exe"}),
            ("Node.js", new []{"node.exe"}),
            ("R", new []{"R.exe", "Rscript.exe"}),
            ("Lua", new []{"lua.exe", "luajit.exe"}),
            ("Tcl", new []{"tclsh.exe", "tclsh86.exe", "tclsh8.6.exe"}),
            ("Julia", new []{"julia.exe"}),
            ("Raku", new []{"raku.exe", "perl6.exe"}),
            ("Groovy", new []{"groovy.exe", "groovyc.exe", "grape.exe"}),

            ("Haskell (GHC)", new []{"ghc.exe", "ghci.exe", "runghc.exe"}),
            ("OCaml", new []{"ocaml.exe", "ocamlc.exe", "ocamlopt.exe", "ocamldebug.exe"}),
            ("Erlang", new []{"erl.exe", "erlc.exe"}),
            ("Elixir", new []{"elixir.exe", "iex.exe", "mix.exe"}),
            ("Lisp (SBCL)", new []{"sbcl.exe"}),
            ("Lisp (CLISP)", new []{"clisp.exe"}),
            ("Clojure", new []{"clojure.exe", "clj.exe"}),
            ("Scheme", new []{"guile.exe", "mit-scheme.exe", "racket.exe"}),

            ("JRuby", new []{"jruby.exe"}),
            ("Jython", new []{"jython.exe"}),

            ("Emscripten (C/C++)", new [] { "emcc.bat", "em++.bat", "emcc", "em++" }),
            ("AssemblyScript", new [] {"asc.cmd", "asc" }),

            ("MATLAB", new []{"matlab.exe"}),
            ("Octave", new []{"octave-cli.exe", "octave.exe"}),
            ("Prolog (SWI-Prolog)", new []{"swipl.exe", "swipl-win.exe"}),

            ("Visual Basic .NET", new []{"vbc.exe"}),

            ("PowerShell", new [] { "powershell.exe", "pwsh.exe" })
        };

        foreach (var (name, executables) in languages)
        {
            List<string> foundPaths = new();

            foreach (var exe in executables)
            {
                var paths = WhereCommand(exe);
                if (paths.Count > 0)
                    foundPaths.AddRange(paths);
            }

            if (foundPaths.Count > 0)
            {
                sb.AppendLine($"{name} is installed at:");
                foreach (var path in foundPaths)
                    sb.AppendLine($"   {path}");
                sb.AppendLine();
            }
        }
    }

    private static List<string> WhereCommand(string exeName)
    {
        try
        {
            var psi = new ProcessStartInfo("where", exeName)
            {
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true
            };

            var proc = Process.Start(psi);
            if (proc == null)
            {
                return new List<string>();
            }
            proc.WaitForExit();

            if (proc.ExitCode == 0)
            {
                var output = proc.StandardOutput.ReadToEnd();
                var lines = output.Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries);
                return new List<string>(lines);
            }
        }
        catch
        {
        }
        return new List<string>();
    }

    static void PrintSectionHeader(StringBuilder sb, string title)
    {
        sb.AppendLine();
        sb.AppendLine($"===== {title.ToUpper()} =====");
        sb.AppendLine();
    }

    static void PrintSubSectionHeader(StringBuilder sb, string title)
    {
        sb.AppendLine();
        sb.AppendLine($"[{title}]");
    }

    static void QueryWMI(
        StringBuilder sb,
        string className,
        List<WmiProperty> properties,
        string? sectionTitle = null,
        string? condition = null)
    {
        try
        {
            if (sectionTitle != null)
                PrintSubSectionHeader(sb, sectionTitle);

            var query = $"SELECT * FROM {className}";
            if (!string.IsNullOrEmpty(condition))
                query += $" WHERE {condition}";

            using var searcher = new ManagementObjectSearcher(query);
            foreach (ManagementObject obj in searcher.Get())
            {
                foreach (var prop in properties)
                {
                    object? value = obj[prop.PropertyName];
                    if (value == null) continue;

                    string formattedValue = prop.Formatter != null
                        ? prop.Formatter(value)
                        : value.ToString();
                    sb.AppendLine($"{prop.DisplayName}: {formattedValue}");
                }
                sb.AppendLine();
            }
        }
        catch (Exception ex)
        {
            sb.AppendLine($"Error querying {className}: {ex.Message}");
        }
    }

    class WmiProperty
    {
        public string PropertyName { get; }
        public string DisplayName { get; }
        public Func<object, string>? Formatter { get; }

        public WmiProperty(string prop, string display, Func<object, string>? formatter = null)
        {
            PropertyName = prop;
            DisplayName = display;
            Formatter = formatter;
        }
    }
}
