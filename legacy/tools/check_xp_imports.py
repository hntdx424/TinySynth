#!/usr/bin/env python3
"""Fail if a TinySynth.exe import or subsystem version is newer than Windows XP."""

import re
import subprocess
import sys

ALLOWED_DLLS = {
    "kernel32.dll",
    "user32.dll",
    "gdi32.dll",
    "comctl32.dll",
    "comdlg32.dll",
    "winmm.dll",
    "msvcrt.dll",
}

# Present in later Windows versions and commonly pulled in by a too-new CRT or SDK.
BANNED_FUNCTIONS = {
    "AcquiresRWLockExclusive",
    "AcquireSRWLockExclusive",
    "AcquireSRWLockShared",
    "AddDllDirectory",
    "CancelIoEx",
    "ChangeWindowMessageFilter",
    "ChangeWindowMessageFilterEx",
    "CloseThreadpool",
    "CloseThreadpoolTimer",
    "CloseThreadpoolWait",
    "CloseThreadpoolWork",
    "CompareStringEx",
    "CreateFile2",
    "CreateSymbolicLinkA",
    "CreateSymbolicLinkW",
    "CreateThreadpool",
    "CreateThreadpoolTimer",
    "CreateThreadpoolWait",
    "CreateThreadpoolWork",
    "EventSetInformation",
    "FindFirstFileNameW",
    "FlsAlloc",
    "FlsFree",
    "FlsGetValue",
    "FlsSetValue",
    "GetDateFormatEx",
    "GetFileInformationByHandleEx",
    "GetFinalPathNameByHandleA",
    "GetFinalPathNameByHandleW",
    "GetLocaleInfoEx",
    "GetTickCount64",
    "GetTimeFormatEx",
    "InitializeConditionVariable",
    "InitializeCriticalSectionEx",
    "InitializeSRWLock",
    "InitOnceBeginInitialize",
    "InitOnceComplete",
    "InitOnceExecuteOnce",
    "IsValidLocaleName",
    "LCIDToLocaleName",
    "LCMapStringEx",
    "LocaleNameToLCID",
    "RegGetValueA",
    "RegGetValueW",
    "ReleaseSRWLockExclusive",
    "ReleaseSRWLockShared",
    "SetFileInformationByHandle",
    "SetProcessDPIAware",
    "SetProcessDpiAwareness",
    "SetThreadpoolTimer",
    "SHGetKnownFolderPath",
    "SleepConditionVariableCS",
    "SleepConditionVariableSRW",
    "SubmitThreadpoolWork",
    "TaskDialog",
    "TaskDialogIndirect",
    "TryAcquireSRWLockExclusive",
    "TryAcquireSRWLockShared",
    "TrySubmitThreadpoolCallback",
    "WakeAllConditionVariable",
    "WakeConditionVariable",
}

BANNED_DLL_PREFIXES = (
    "api-ms-win-",
    "ucrtbase",
    "vcruntime",
    "msvcp",
    "msvcr1",
    "msvcr70",
    "msvcr71",
    "msvcr80",
    "msvcr90",
    "dwmapi",
    "kernelbase",
)


def fail(message):
    print("XP check failed: " + message, file=sys.stderr)
    return 1


def parse_versions(text):
    def field(name):
        match = re.search(r"^" + name + r"\s+(\d+)\s*$", text, re.M)
        if not match:
            raise SystemExit(fail("missing " + name))
        return int(match.group(1))

    return {
        "os_major": field("MajorOSystemVersion"),
        "os_minor": field("MinorOSystemVersion"),
        "sub_major": field("MajorSubsystemVersion"),
        "sub_minor": field("MinorSubsystemVersion"),
    }


def parse_imports(text):
    dlls = []
    current = None
    for line in text.splitlines():
        dll_match = re.search(r"DLL Name:\s+(\S+)", line)
        if dll_match:
            current = {"dll": dll_match.group(1), "functions": []}
            dlls.append(current)
            continue
        if current is None:
            continue
        fn_match = re.match(r"\s+[0-9a-fA-F]+\s+\d+\s+([A-Za-z_][A-Za-z0-9_@?]*)\s*$", line)
        if fn_match:
            current["functions"].append(fn_match.group(1))
    return dlls


def main():
    if len(sys.argv) != 3:
        print("usage: check_xp_imports.py TINYSYNTH.EXE OBJDUMP", file=sys.stderr)
        return 2
    exe, objdump = sys.argv[1], sys.argv[2]
    text = subprocess.check_output([objdump, "-p", exe], text=True, errors="replace")
    if "pei-i386" not in text.splitlines()[0] and "file format pei-i386" not in text:
        return fail("expected a 32-bit PE (pei-i386), got: " + text.splitlines()[0])
    if not re.search(r"Subsystem\s+00000002\b", text):
        return fail("expected the Windows GUI subsystem")

    versions = parse_versions(text)
    print(
        "PE: pei-i386  OS {os_major}.{os_minor}  subsystem {sub_major}.{sub_minor} (GUI)".format(
            **versions
        )
    )
    too_new = (versions["sub_major"], versions["sub_minor"]) > (5, 1)
    if too_new or (versions["os_major"], versions["os_minor"]) > (5, 1):
        return fail("OS/subsystem version must be 5.1 (Windows XP) or lower")

    imports = parse_imports(text)
    if not imports:
        return fail("no import table found")

    problems = []
    for entry in imports:
        dll = entry["dll"]
        key = dll.lower()
        print("  {0}: {1} functions".format(dll, len(entry["functions"])))
        for name in entry["functions"]:
            print("    {0}".format(name))
        if key not in ALLOWED_DLLS or any(key.startswith(prefix) for prefix in BANNED_DLL_PREFIXES):
            problems.append("disallowed DLL " + dll)
        for name in entry["functions"]:
            if name in BANNED_FUNCTIONS:
                problems.append(dll + "!" + name)
    if problems:
        return fail("imports not available on Windows XP: " + ", ".join(problems))
    print("XP import check: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
