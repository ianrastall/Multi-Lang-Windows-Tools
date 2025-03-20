#![cfg(windows)]

use std::collections::HashMap;
use std::fs::File;
use std::io::Write;
use std::process::Command;

use codepage;
use encoding_rs::Encoding;
use sys_locale::get_locale;
use wmi::{COMLibrary, Variant, WMIConnection};
use winapi::um::winnls::GetACP;

struct WmiProperty {
    property_name: &'static str,
    display_name: &'static str,
    formatter: Option<Box<dyn Fn(&Variant) -> String>>,
}

impl WmiProperty {
    fn new(property_name: &'static str, display_name: &'static str) -> Self {
        Self {
            property_name,
            display_name,
            formatter: None,
        }
    }

    fn with_formatter<F: 'static + Fn(&Variant) -> String>(mut self, formatter: F) -> Self {
        self.formatter = Some(Box::new(formatter));
        self
    }

    fn format(&self, value: &Variant) -> String {
        if let Some(f) = &self.formatter {
            f(value)
        } else {
            match value {
                Variant::String(s) => s.clone(),
                Variant::UI8(n) => n.to_string(),
                Variant::I8(n) => n.to_string(),
                Variant::UI4(n) => n.to_string(),
                Variant::I4(n) => n.to_string(),
                Variant::UI2(n) => n.to_string(),
                Variant::I2(n) => n.to_string(),
                Variant::R4(n) => n.to_string(),
                Variant::R8(n) => n.to_string(),
                Variant::Bool(b) => b.to_string(),
                Variant::Null => "Null".to_string(),
                _ => format!("{:?}", value),
            }
        }
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let com_con = COMLibrary::new()?;
    let wmi_con = WMIConnection::new(com_con)?;
    let mut output = String::new();

    print_system_summary(&mut output, &wmi_con);
    print_hardware_resources(&mut output, &wmi_con);
    print_components(&mut output, &wmi_con);
    print_software_environment(&mut output, &wmi_con);
    print_locale_and_encoding(&mut output);
    print_installed_languages(&mut output);

    File::create("system_info.txt")?.write_all(output.as_bytes())?;
    Ok(())
}

fn add_section_header(sb: &mut String, title: &str) {
    sb.push_str(&format!("\n===== {} =====\n\n", title.to_uppercase()));
}

fn add_sub_section_header(sb: &mut String, title: &str) {
    sb.push_str(&format!("\n[{}]\n", title));
}

fn query_wmi(
    sb: &mut String,
    wmi_con: &WMIConnection,
    class_name: &str,
    properties: &[WmiProperty],
    section_title: Option<&str>,
    condition: Option<&str>,
) {
    if let Some(title) = section_title {
        add_sub_section_header(sb, title);
    }

    let query = match condition {
        Some(cond) => format!("SELECT * FROM {} WHERE {}", class_name, cond),
        None => format!("SELECT * FROM {}", class_name),
    };

    match wmi_con.raw_query::<HashMap<String, Variant>>(&query) {
        Ok(results) => {
            for obj in results {
                for prop in properties {
                    if let Some(value) = obj.get(prop.property_name) {
                        let formatted = prop.format(value);
                        sb.push_str(&format!("{}: {}\n", prop.display_name, formatted));
                    }
                }
                sb.push('\n');
            }
        }
        Err(e) => sb.push_str(&format!("Error querying {}: {}\n", class_name, e)),
    }
}

fn print_system_summary(sb: &mut String, wmi_con: &WMIConnection) {
    add_section_header(sb, "System Summary");

    let os_props = [
        WmiProperty::new("Caption", "OS Name"),
        WmiProperty::new("Version", "Version"),
        WmiProperty::new("BuildNumber", "Build"),
        WmiProperty::new("OSArchitecture", "Architecture"),
        WmiProperty::new("SerialNumber", "Serial"),
        WmiProperty::new("InstallDate", "Install Date"),
    ];
    query_wmi(sb, wmi_con, "Win32_OperatingSystem", &os_props, None, None);

    let bios_props = [
        WmiProperty::new("Manufacturer", "BIOS Vendor"),
        WmiProperty::new("Name", "BIOS Version"),
        WmiProperty::new("ReleaseDate", "Release Date"),
        WmiProperty::new("SMBIOSBIOSVersion", "SMBIOS Version"),
    ];
    query_wmi(sb, wmi_con, "Win32_BIOS", &bios_props, None, None);

    let cs_props = [
        WmiProperty::new("Manufacturer", "System Manufacturer"),
        WmiProperty::new("Model", "System Model"),
        WmiProperty::new("SystemType", "System Type"),
        WmiProperty::new("TotalPhysicalMemory", "Total Physical Memory (GB)")
            .with_formatter(format_memory_gb),
    ];
    query_wmi(sb, wmi_con, "Win32_ComputerSystem", &cs_props, None, None);
}

fn print_hardware_resources(sb: &mut String, wmi_con: &WMIConnection) {
    add_section_header(sb, "Hardware Resources");

    let mem_props = [
        WmiProperty::new("Capacity", "Memory Capacity (GB)")
            .with_formatter(format_memory_gb),
        WmiProperty::new("Speed", "Speed (MHz)"),
        WmiProperty::new("Manufacturer", "Manufacturer"),
    ];
    query_wmi(sb, wmi_con, "Win32_PhysicalMemory", &mem_props, Some("Memory Devices"), None);

    let cpu_props = [
        WmiProperty::new("Name", "Processor"),
        WmiProperty::new("NumberOfCores", "Cores"),
        WmiProperty::new("NumberOfLogicalProcessors", "Logical Processors"),
        WmiProperty::new("MaxClockSpeed", "Max Speed (MHz)"),
        WmiProperty::new("L2CacheSize", "L2 Cache (MB)")
            .with_formatter(format_cache_mb),
        WmiProperty::new("L3CacheSize", "L3 Cache (MB)")
            .with_formatter(format_cache_mb),
    ];
    query_wmi(sb, wmi_con, "Win32_Processor", &cpu_props, Some("Processor Details"), None);
}

fn print_components(sb: &mut String, wmi_con: &WMIConnection) {
    add_section_header(sb, "Components");

    let display_props = [
        WmiProperty::new("Name", "Adapter"),
        WmiProperty::new("AdapterRAM", "VRAM (GB)")
            .with_formatter(format_memory_gb),
        WmiProperty::new("DriverVersion", "Driver Version"),
        WmiProperty::new("VideoProcessor", "GPU Chip"),
    ];
    query_wmi(sb, wmi_con, "Win32_VideoController", &display_props, Some("Display"), None);

    let storage_props = [
        WmiProperty::new("Model", "Disk Model"),
        WmiProperty::new("Size", "Capacity (GB)")
            .with_formatter(format_memory_gb),
        WmiProperty::new("InterfaceType", "Interface"),
    ];
    query_wmi(sb, wmi_con, "Win32_DiskDrive", &storage_props, Some("Storage"), None);
}

fn print_software_environment(sb: &mut String, wmi_con: &WMIConnection) {
    add_section_header(sb, "Software Environment");

    let update_props = [
        WmiProperty::new("HotFixID", "Update"),
        WmiProperty::new("InstalledOn", "Install Date"),
        WmiProperty::new("Description", "Description"),
    ];
    query_wmi(sb, wmi_con, "Win32_QuickFixEngineering", &update_props, Some("Windows Updates"), None);

    let net_props = [
        WmiProperty::new("Description", "Adapter"),
        WmiProperty::new("IPAddress", "IP Address"),
        WmiProperty::new("MACAddress", "MAC"),
    ];
    query_wmi(sb, wmi_con, "Win32_NetworkAdapterConfiguration", &net_props, Some("Network"), Some("IPEnabled = TRUE"));
}

fn print_locale_and_encoding(sb: &mut String) {
    add_section_header(sb, "Locale and Encoding");
    sb.push_str(&format!("System Locale: {}\n", get_system_locale()));
    sb.push_str(&format!("Default Encoding: {}\n", get_system_encoding().name()));
}

fn print_installed_languages(sb: &mut String) {
    add_section_header(sb, "Installed Programming Languages");

    let languages = [
        ("C", &["cl.exe", "gcc.exe", "clang.exe"][..]),
        ("C++", &["cl.exe", "g++.exe", "clang++.exe"][..]),
        ("C#", &["csc.exe", "dotnet.exe"][..]),
        ("D", &["dmd.exe", "ldc2.exe", "gdc.exe"][..]),
        ("Java", &["java.exe", "javac.exe"][..]),
        ("Kotlin", &["kotlinc.exe", "kotlinc-jvm.exe", "kotlin.bat"][..]),
        ("Scala", &["scala.exe", "scalac.exe"][..]),
        ("Go", &["go.exe"][..]),
        ("Rust", &["rustc.exe", "cargo.exe"][..]),
        ("Swift", &["swift.exe", "swiftc.exe"][..]),
        ("F#", &["fsc.exe", "fsi.exe", "dotnet.exe"][..]),
        ("Fortran", &["gfortran.exe", "ifort.exe"][..]),
        ("Pascal", &["fpc.exe", "ppc386.exe", "ppcx64.exe"][..]),
        ("Delphi", &["dcc32.exe", "dcc64.exe", "bds.exe"][..]),
        ("Ada", &["gnat.exe", "gcc.exe"][..]),
        ("Objective-C", &["gcc.exe", "clang.exe"][..]),
        ("Zig", &["zig.exe"][..]),
        ("Nim", &["nim.exe", "nimble.exe"][..]),
        ("Python", &["python.exe", "python3.exe", "pypy.exe", "pypy3.exe"][..]),
        ("Perl", &["perl.exe"][..]),
        ("PHP", &["php.exe", "php-cgi.exe"][..]),
        ("Ruby", &["ruby.exe", "irb.exe"][..]),
        ("Node.js", &["node.exe"][..]),
        ("R", &["R.exe", "Rscript.exe"][..]),
        ("Lua", &["lua.exe", "luajit.exe"][..]),
        ("Tcl", &["tclsh.exe", "tclsh86.exe", "tclsh8.6.exe"][..]),
        ("Julia", &["julia.exe"][..]),
        ("Raku", &["raku.exe", "perl6.exe"][..]),
        ("Groovy", &["groovy.exe", "groovyc.exe", "grape.exe"][..]),
        ("Haskell (GHC)", &["ghc.exe", "ghci.exe", "runghc.exe"][..]),
        ("OCaml", &["ocaml.exe", "ocamlc.exe", "ocamlopt.exe", "ocamldebug.exe"][..]),
        ("Erlang", &["erl.exe", "erlc.exe"][..]),
        ("Elixir", &["elixir.exe", "iex.exe", "mix.exe"][..]),
        ("Lisp (SBCL)", &["sbcl.exe"][..]),
        ("Lisp (CLISP)", &["clisp.exe"][..]),
        ("Clojure", &["clojure.exe", "clj.exe"][..]),
        ("Scheme", &["guile.exe", "mit-scheme.exe", "racket.exe"][..]),
        ("JRuby", &["jruby.exe"][..]),
        ("Jython", &["jython.exe"][..]),
        ("Emscripten (C/C++)", &["emcc.bat", "em++.bat", "emcc", "em++"][..]),
        ("AssemblyScript", &["asc.cmd", "asc"][..]),
        ("MATLAB", &["matlab.exe"][..]),
        ("Octave", &["octave-cli.exe", "octave.exe"][..]),
        ("Prolog (SWI-Prolog)", &["swipl.exe", "swipl-win.exe"][..]),
        ("Visual Basic .NET", &["vbc.exe"][..]),
        ("PowerShell", &["powershell.exe", "pwsh.exe"][..]),
    ];

    for (name, candidates) in languages.iter() {
        let mut found = Vec::new();
        for exe in *candidates {
            found.extend(where_command(exe));
        }
        if !found.is_empty() {
            sb.push_str(&format!("{} is installed at:\n", name));
            for path in found {
                sb.push_str(&format!("   {}\n", path));
            }
            sb.push('\n');
        }
    }
}

fn where_command(exe: &str) -> Vec<String> {
    Command::new("where.exe")
        .arg(exe)
        .output()
        .ok()
        .and_then(|output| {
            if output.status.success() {
                let stdout = String::from_utf8_lossy(&output.stdout);
                Some(stdout.lines().map(|s| s.trim().to_string()).collect())
            } else {
                None
            }
        })
        .unwrap_or_default()
}

fn format_memory_gb(v: &Variant) -> String {
    match v {
        Variant::UI8(n) => format!("{:.1}", *n as f64 / (1024.0 * 1024.0 * 1024.0)),
        Variant::I8(n) => format!("{:.1}", *n as f64 / (1024.0 * 1024.0 * 1024.0)),
        Variant::UI4(n) => format!("{:.1}", *n as f64 / (1024.0 * 1024.0 * 1024.0)),
        Variant::I4(n) => format!("{:.1}", *n as f64 / (1024.0 * 1024.0 * 1024.0)),
        _ => "N/A".to_string(),
    }
}

fn format_cache_mb(v: &Variant) -> String {
    match v {
        Variant::UI8(n) => format!("{:.1}", *n as f64 / 1024.0),
        Variant::I8(n) => format!("{:.1}", *n as f64 / 1024.0),
        Variant::UI4(n) => format!("{:.1}", *n as f64 / 1024.0),
        Variant::I4(n) => format!("{:.1}", *n as f64 / 1024.0),
        _ => "N/A".to_string(),
    }
}

fn get_system_locale() -> String {
    get_locale().unwrap_or_else(|| "unknown".to_string())
}

fn get_system_encoding() -> &'static Encoding {
    unsafe {
        let cp = GetACP();
        codepage::to_encoding(cp as u16)
            .unwrap_or(encoding_rs::WINDOWS_1252)
    }
}