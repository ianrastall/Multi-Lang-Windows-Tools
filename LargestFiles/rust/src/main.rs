use std::ffi::CString;
use std::fs::{File, OpenOptions};
use std::io::{BufRead, BufReader, Write};
use std::mem;

use windows_sys::Win32::Foundation::INVALID_HANDLE_VALUE;
use windows_sys::Win32::System::Console::{AllocConsole, GetConsoleWindow};
use windows_sys::Win32::System::SystemInformation::GetTickCount;
use windows_sys::Win32::Storage::FileSystem::{
    FindClose, FindFirstFileA, FindNextFileA, GetDriveTypeA, GetLogicalDrives, WIN32_FIND_DATAA,
    FILE_ATTRIBUTE_DIRECTORY, FILE_ATTRIBUTE_HIDDEN, FILE_ATTRIBUTE_REPARSE_POINT,
    FILE_ATTRIBUTE_SYSTEM, FILE_ATTRIBUTE_TEMPORARY,
};

// We only keep the drive types we actually use:
const DRIVE_REMOVABLE: u32 = 2;
const DRIVE_FIXED: u32 = 3;

const MAX_PATH: usize = 260;

#[derive(Debug)]
struct FileEntry {
    path: String,
    size: u64,
}

struct FileList {
    entries: Vec<FileEntry>,
}

impl FileList {
    fn new() -> Self {
        Self { entries: Vec::new() }
    }
    fn add(&mut self, path: &str, size: u64) {
        self.entries.push(FileEntry {
            path: path.to_string(),
            size,
        });
    }
}

fn init_console() {
    unsafe {
        if GetConsoleWindow() == 0 {
            AllocConsole();
        }
    }
}

fn enumerate_drives() -> Vec<String> {
    let mut results = Vec::new();
    println!("[Drive Scan]");

    unsafe {
        let mask = GetLogicalDrives();
        for c in b'A'..=b'Z' {
            if mask & (1 << (c - b'A')) != 0 {
                let drive = format!("{}:\\", c as char);
                let c_drive = CString::new(drive.clone()).unwrap();
                let drive_type = GetDriveTypeA(c_drive.as_ptr() as *const u8);

                if drive_type == DRIVE_FIXED || drive_type == DRIVE_REMOVABLE {
                    println!("Including drive: {}", drive);
                    results.push(drive);
                }
            }
        }
    }
    results
}

fn process_directory(path: &str, list: &mut FileList) {
    let search_pattern = if path.ends_with('\\') || path.ends_with('/') {
        format!("{}*", path)
    } else {
        format!("{}\\*", path)
    };

    let c_search = match CString::new(search_pattern.clone()) {
        Ok(s) => s,
        Err(_) => return,
    };

    let mut data: WIN32_FIND_DATAA = unsafe { mem::zeroed() };
    let handle = unsafe { FindFirstFileA(c_search.as_ptr() as *const u8, &mut data) };
    if handle == INVALID_HANDLE_VALUE {
        return;
    }

    loop {
        let raw = &data.cFileName;
        let len = raw.iter().position(|&b| b == 0).unwrap_or(MAX_PATH);
        let filename = String::from_utf8_lossy(&raw[..len]).to_string();

        if filename != "." && filename != ".." {
            let attrs = data.dwFileAttributes;

            // Ignore system / hidden / temporary / reparse point files
            if (attrs & FILE_ATTRIBUTE_SYSTEM) == 0
                && (attrs & FILE_ATTRIBUTE_HIDDEN) == 0
                && (attrs & FILE_ATTRIBUTE_TEMPORARY) == 0
                && (attrs & FILE_ATTRIBUTE_REPARSE_POINT) == 0
            {
                let full_path = if path.ends_with('\\') || path.ends_with('/') {
                    format!("{}{}", path, filename)
                } else {
                    format!("{}\\{}", path, filename)
                };

                if (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0 {
                    process_directory(&full_path, list);
                } else {
                    let high = data.nFileSizeHigh as u64;
                    let low = data.nFileSizeLow as u64;
                    let size = (high << 32) | low;
                    list.add(&full_path, size);
                }
            }
        }

        let ok = unsafe { FindNextFileA(handle, &mut data) };
        if ok == 0 {
            break;
        }
    }

    unsafe {
        FindClose(handle);
    }
}

fn main() {
    init_console();
    println!("File Scanner");
    println!("----------------------------------------");

    let drives = enumerate_drives();
    if drives.is_empty() {
        eprintln!("No suitable drives found!");
        return;
    }

    let outfile = "largest_files.txt";
    let _ = File::create(outfile);

    for drive in drives {
        println!("\nProcessing {}", drive);
        let mut flist = FileList::new();

        let start = unsafe { GetTickCount() };
        process_directory(&drive, &mut flist);
        let end = unsafe { GetTickCount() };

        let duration_ms = end.wrapping_sub(start);
        println!("Scanned {} in {:.1} seconds", drive, duration_ms as f64 / 1000.0);
        println!("Found {} files", flist.entries.len());

        if !flist.entries.is_empty() {
            flist.entries.sort_by(|a, b| b.size.cmp(&a.size));

            let limit = flist.entries.len().min(100);
            if let Ok(mut fp) = OpenOptions::new().append(true).write(true).open(outfile) {
                let drive_display = &drive[..2];
                writeln!(fp, "Largest files on {}:", drive_display).ok();

                for i in 0..limit {
                    let mb = flist.entries[i].size as f64 / (1024.0 * 1024.0);
                    writeln!(fp, "{}: {:.2} MB", flist.entries[i].path, mb).ok();
                }
                writeln!(fp).ok();
            }
        }
    }

    println!("\nScan complete. Results saved to {}", outfile);
    println!("Press Enter to exit...");
    let mut reader = BufReader::new(std::io::stdin());
    let mut dummy = String::new();
    let _ = reader.read_line(&mut dummy);
}
