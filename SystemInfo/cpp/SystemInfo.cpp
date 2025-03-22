#include <Windows.h>
#include <comdef.h>
#include <Wbemidl.h>
#include <sstream>
#include <iomanip>
#include <vector>
#include <functional>
#include <locale>
#include <algorithm>
#include <cstdio>
#include <string>
#include <iostream>

// Note: MinGW ignores #pragma comment(lib, ...) so link with:
// -loleaut32 -lole32 -lwbemuuid -lshlwapi

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "OleAut32.lib")

// RAII for COM initialization
class ComInitializer {
    HRESULT hr;
public:
    ComInitializer() {
        hr = CoInitializeEx(0, COINIT_MULTITHREADED);
        if (SUCCEEDED(hr)) {
            hr = CoInitializeSecurity(
                NULL,
                -1,
                NULL,
                NULL,
                RPC_C_AUTHN_LEVEL_DEFAULT,
                RPC_C_IMP_LEVEL_IMPERSONATE,
                NULL,
                EOAC_NONE,
                NULL
            );
        }
    }
    ~ComInitializer() {
        if (SUCCEEDED(hr))
            CoUninitialize();
    }
    bool IsOK() const { return SUCCEEDED(hr); }
};

// RAII wrapper for WMI connection
class WmiConnection {
    IWbemLocator* pLoc = nullptr;
    IWbemServices* pSvc = nullptr;
public:
    WmiConnection() {
        HRESULT hres = CoCreateInstance(
            CLSID_WbemLocator,
            0,
            CLSCTX_INPROC_SERVER,
            IID_IWbemLocator,
            (LPVOID*)&pLoc
        );
        if (FAILED(hres)) {
            return;
        }
        hres = pLoc->ConnectServer(
            _bstr_t(L"ROOT\\CIMV2"),
            NULL,
            NULL,
            NULL,
            0,
            NULL,
            NULL,
            &pSvc
        );
        if (FAILED(hres)) {
            pLoc->Release();
            pLoc = nullptr;
            return;
        }
        hres = CoSetProxyBlanket(
            pSvc,
            RPC_C_AUTHN_WINNT,
            RPC_C_AUTHZ_NONE,
            NULL,
            RPC_C_AUTHN_LEVEL_CALL,
            RPC_C_IMP_LEVEL_IMPERSONATE,
            NULL,
            EOAC_NONE
        );
    }
    ~WmiConnection() {
        if (pSvc) pSvc->Release();
        if (pLoc) pLoc->Release();
    }
    IWbemServices* operator->() { return pSvc; }
    bool IsConnected() const { return pSvc != nullptr; }
};

// Structure for WMI properties
struct WmiProperty {
    const wchar_t* name;
    const wchar_t* displayName;
    std::function<std::wstring(VARIANT&)> formatter;
};

// Formatter for memory in GB
std::wstring FormatMemoryGB(VARIANT& v) {
    ULONGLONG val = v.ullVal;
    double gb = val / (1024.0 * 1024.0 * 1024.0);
    std::wstringstream ss;
    ss << std::fixed << std::setprecision(1) << gb;
    return ss.str();
}

// Formatter for cache sizes in MB
std::wstring FormatCacheMB(VARIANT& v) {
    ULONG val = v.ulVal;
    double mb = val / 1024.0;
    std::wstringstream ss;
    ss << std::fixed << std::setprecision(1) << mb;
    return ss.str();
}

// Generic WMI query function with safe VARIANT-to-string conversion
void QueryWMI(
    IWbemServices* pSvc,
    const std::wstring& className,
    const std::vector<WmiProperty>& properties,
    std::wstringstream& output,
    const std::wstring& section = L"",
    const std::wstring& condition = L""
) {
    if (!section.empty())
        output << L"\n[" << section << L"]\n";

    std::wstring query = L"SELECT * FROM " + className;
    if (!condition.empty())
        query += L" WHERE " + condition;

    IEnumWbemClassObject* pEnumerator = nullptr;
    HRESULT hres = pSvc->ExecQuery(
        bstr_t(L"WQL"),
        bstr_t(query.c_str()),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        NULL,
        &pEnumerator
    );

    if (FAILED(hres)) {
        output << L"Error querying " << className << L": " << hres << L"\n";
        return;
    }

    IWbemClassObject* pclsObj = nullptr;
    ULONG uReturn = 0;
    while (pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn) == S_OK) {
        for (const auto& prop : properties) {
            VARIANT vtProp;
            VariantInit(&vtProp);
            HRESULT hr = pclsObj->Get(prop.name, 0, &vtProp, 0, 0);
            if (SUCCEEDED(hr)) {
                std::wstring value;
                if (prop.formatter) {
                    value = prop.formatter(vtProp);
                } else {
                    VARIANT vtCopy;
                    VariantInit(&vtCopy);
                    HRESULT hrConv = VariantChangeType(&vtCopy, &vtProp, 0, VT_BSTR);
                    if (SUCCEEDED(hrConv)) {
                        _bstr_t bstrVal(vtCopy.bstrVal, false);
                        value = static_cast<const wchar_t*>(bstrVal);
                    } else {
                        value = L"[Conversion Error]";
                    }
                    VariantClear(&vtCopy);
                }
                output << prop.displayName << L": " << value << L"\n";
            }
            VariantClear(&vtProp);
        }
        output << L"\n";
        pclsObj->Release();
    }
    pEnumerator->Release();
}

// SYSTEM SUMMARY
void PrintSystemSummary(IWbemServices* pSvc, std::wstringstream& output) {
    output << L"\n===== SYSTEM SUMMARY =====\n\n";

    QueryWMI(pSvc, L"Win32_OperatingSystem", {
        {L"Caption", L"OS Name", nullptr},
        {L"Version", L"Version", nullptr},
        {L"BuildNumber", L"Build", nullptr},
        {L"OSArchitecture", L"Architecture", nullptr},
        {L"SerialNumber", L"Serial", nullptr},
        {L"InstallDate", L"Install Date", nullptr}
    }, output);

    QueryWMI(pSvc, L"Win32_BIOS", {
        {L"Manufacturer", L"BIOS Vendor", nullptr},
        {L"Name", L"BIOS Version", nullptr},
        {L"ReleaseDate", L"Release Date", nullptr},
        {L"SMBIOSBIOSVersion", L"SMBIOS Version", nullptr}
    }, output);

    QueryWMI(pSvc, L"Win32_ComputerSystem", {
        {L"Manufacturer", L"System Manufacturer", nullptr},
        {L"Model", L"System Model", nullptr},
        {L"SystemType", L"System Type", nullptr},
        {L"TotalPhysicalMemory", L"Total Physical Memory (GB)", FormatMemoryGB}
    }, output);
}

// HARDWARE RESOURCES
void PrintHardwareResources(IWbemServices* pSvc, std::wstringstream& output) {
    output << L"\n===== HARDWARE RESOURCES =====\n\n";

    QueryWMI(pSvc, L"Win32_PhysicalMemory", {
        {L"Capacity", L"Memory Capacity (GB)", FormatMemoryGB},
        {L"Speed", L"Speed (MHz)", nullptr},
        {L"Manufacturer", L"Manufacturer", nullptr}
    }, output, L"Memory Devices");

    QueryWMI(pSvc, L"Win32_Processor", {
        {L"Name", L"Processor", nullptr},
        {L"NumberOfCores", L"Cores", nullptr},
        {L"NumberOfLogicalProcessors", L"Logical Processors", nullptr},
        {L"MaxClockSpeed", L"Max Speed (MHz)", nullptr},
        {L"L2CacheSize", L"L2 Cache (MB)", FormatCacheMB},
        {L"L3CacheSize", L"L3 Cache (MB)", FormatCacheMB}
    }, output, L"Processor Details");
}

// COMPONENTS
void PrintComponents(IWbemServices* pSvc, std::wstringstream& output) {
    output << L"\n===== COMPONENTS =====\n\n";

    QueryWMI(pSvc, L"Win32_VideoController", {
        {L"Name", L"Adapter", nullptr},
        {L"AdapterRAM", L"VRAM (GB)", FormatMemoryGB},
        {L"DriverVersion", L"Driver Version", nullptr},
        {L"VideoProcessor", L"GPU Chip", nullptr}
    }, output, L"Display");

    QueryWMI(pSvc, L"Win32_DiskDrive", {
        {L"Model", L"Disk Model", nullptr},
        {L"Size", L"Capacity (GB)", FormatMemoryGB},
        {L"InterfaceType", L"Interface", nullptr}
    }, output, L"Storage");
}

// SOFTWARE ENVIRONMENT
void PrintSoftwareEnvironment(IWbemServices* pSvc, std::wstringstream& output) {
    output << L"\n===== SOFTWARE ENVIRONMENT =====\n\n";

    QueryWMI(pSvc, L"Win32_QuickFixEngineering", {
        {L"HotFixID", L"Update", nullptr},
        {L"InstalledOn", L"Install Date", nullptr},
        {L"Description", L"Description", nullptr}
    }, output, L"Windows Updates");

    QueryWMI(pSvc, L"Win32_NetworkAdapterConfiguration", {
        {L"Description", L"Adapter", nullptr},
        {L"IPAddress", L"IP Address", nullptr},
        {L"MACAddress", L"MAC", nullptr}
    }, output, L"Network", L"IPEnabled = TRUE");
}

// LOCALE AND ENCODING
void PrintLocaleAndEncoding(std::wstringstream& output) {
    output << L"\n===== LOCALE AND ENCODING =====\n\n";
    wchar_t localeName[85];
    if (GetUserDefaultLocaleName(localeName, 85))
        output << L"System Locale: " << localeName << L"\n";
    else
        output << L"System Locale: unknown\n";

    UINT cp = GetACP();
    output << L"Default Encoding: Code Page " << cp << L"\n";
}

// Execute where.exe to locate executables, redirecting error output to nul
std::vector<std::wstring> ExecuteWhere(const std::wstring& exe) {
    std::vector<std::wstring> paths;
    std::wstring cmd = L"where.exe " + exe + L" 2>nul";
    FILE* pipe = _wpopen(cmd.c_str(), L"rt");
    if (pipe) {
        wchar_t buffer[512];
        while (fgetws(buffer, 512, pipe)) {
            std::wstring line(buffer);
            line.erase(std::remove(line.begin(), line.end(), L'\n'), line.end());
            line.erase(std::remove(line.begin(), line.end(), L'\r'), line.end());
            if (!line.empty())
                paths.push_back(line);
        }
        _pclose(pipe);
    }
    return paths;
}

// INSTALLED PROGRAMMING LANGUAGES (Enhanced list)
void PrintInstalledLanguages(std::wstringstream& output) {
    output << L"\n===== INSTALLED PROGRAMMING LANGUAGES =====\n\n";
    output << L"Note: Detection requires language executables to be in the system's PATH.\n\n";

    const std::vector<std::pair<std::wstring, std::vector<std::wstring>>> languages = {
        {L"C", {L"cl.exe", L"gcc.exe", L"clang.exe"}},
        {L"C++", {L"cl.exe", L"g++.exe", L"clang++.exe"}},
        {L"C#", {L"csc.exe", L"dotnet.exe"}},
        {L"D", {L"dmd.exe", L"ldc2.exe", L"gdc.exe"}},
        {L"Java", {L"java.exe", L"javac.exe"}},
        {L"Kotlin", {L"kotlinc.exe", L"kotlinc-jvm.exe", L"kotlin.bat"}},
        {L"Scala", {L"scala.exe", L"scalac.exe"}},
        {L"Go", {L"go.exe"}},
        {L"Rust", {L"rustc.exe", L"cargo.exe"}},
        {L"Swift", {L"swift.exe", L"swiftc.exe"}},
        {L"F#", {L"fsc.exe", L"fsi.exe", L"dotnet.exe"}},
        {L"Fortran", {L"gfortran.exe", L"ifort.exe"}},
        {L"Pascal", {L"fpc.exe", L"ppc386.exe", L"ppcx64.exe"}},
        {L"Delphi", {L"dcc32.exe", L"dcc64.exe", L"bds.exe"}},
        {L"Ada", {L"gnat.exe", L"gcc.exe"}},
        {L"Objective-C", {L"gcc.exe", L"clang.exe"}},
        {L"Zig", {L"zig.exe"}},
        {L"Nim", {L"nim.exe", L"nimble.exe"}},
        {L"Python", {L"python.exe", L"python3.exe", L"pypy.exe", L"pypy3.exe", L"py.exe"}},
        {L"Perl", {L"perl.exe"}},
        {L"PHP", {L"php.exe", L"php-cgi.exe"}},
        {L"Ruby", {L"ruby.exe", L"irb.exe"}},
        {L"Node.js", {L"node.exe"}},
        {L"TypeScript", {L"tsc.exe", L"ts-node.exe"}},
        {L"R", {L"R.exe", L"Rscript.exe"}},
        {L"Lua", {L"lua.exe", L"luajit.exe"}},
        {L"Tcl", {L"tclsh.exe", L"tclsh86.exe", L"tclsh8.6.exe"}},
        {L"Julia", {L"julia.exe"}},
        {L"Raku", {L"raku.exe", L"perl6.exe"}},
        {L"Groovy", {L"groovy.exe", L"groovyc.exe", L"grape.exe"}},
        {L"Haskell (GHC)", {L"ghc.exe", L"ghci.exe", L"runghc.exe"}},
        {L"OCaml", {L"ocaml.exe", L"ocamlc.exe", L"ocamlopt.exe", L"ocamldebug.exe"}},
        {L"Erlang", {L"erl.exe", L"erlc.exe"}},
        {L"Elixir", {L"elixir.exe", L"iex.exe", L"mix.exe"}},
        {L"Lisp (SBCL)", {L"sbcl.exe"}},
        {L"Lisp (CLISP)", {L"clisp.exe"}},
        {L"Clojure", {L"clojure.exe", L"clj.exe"}},
        {L"Scheme", {L"guile.exe", L"mit-scheme.exe", L"racket.exe", L"chicken.exe"}},
        {L"JRuby", {L"jruby.exe"}},
        {L"Jython", {L"jython.exe"}},
        {L"Emscripten (C/C++)", {L"emcc.bat", L"em++.bat", L"emcc", L"em++"}},
        {L"AssemblyScript", {L"asc.cmd", L"asc"}},
        {L"MATLAB", {L"matlab.exe"}},
        {L"Octave", {L"octave-cli.exe", L"octave.exe"}},
        {L"Prolog (SWI-Prolog)", {L"swipl.exe", L"swipl-win.exe"}},
        {L"Visual Basic .NET", {L"vbc.exe"}},
        {L"PowerShell", {L"powershell.exe", L"pwsh.exe"}}
    };

    for (const auto& lang : languages) {
        std::vector<std::wstring> found;
        for (const auto& exe : lang.second) {
            auto paths = ExecuteWhere(exe);
            found.insert(found.end(), paths.begin(), paths.end());
        }
        if (!found.empty()) {
            output << lang.first << L" is installed at:\n";
            for (const auto& path : found)
                output << L"   " << path << L"\n";
            output << L"\n";
        }
    }
}

int main() {
    ComInitializer comInit;
    if (!comInit.IsOK()) {
        std::wcerr << L"COM initialization failed!\n";
        return 1;
    }

    WmiConnection wmi;
    if (!wmi.IsConnected()) {
        std::wcerr << L"WMI connection failed!\n";
        return 1;
    }

    std::wstringstream output;

    PrintSystemSummary(wmi.operator->(), output);
    PrintHardwareResources(wmi.operator->(), output);
    PrintComponents(wmi.operator->(), output);
    PrintSoftwareEnvironment(wmi.operator->(), output);
    PrintLocaleAndEncoding(output);
    PrintInstalledLanguages(output);

    HANDLE hFile = CreateFileW(L"system_info.txt", GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        std::wstring data = output.str();
        DWORD written;
        // Write UTF-16 LE BOM (optional)
        const wchar_t BOM = 0xFEFF;
        WriteFile(hFile, &BOM, sizeof(BOM), &written, NULL);
        // Write content
        WriteFile(hFile, data.c_str(), (DWORD)(data.size() * sizeof(wchar_t)), &written, NULL);
        CloseHandle(hFile);
        std::wcout << L"File written to system_info.txt\n";
    } else {
        std::wcout << L"Error creating file: " << GetLastError() << L"\n";
    }

    return 0;
}