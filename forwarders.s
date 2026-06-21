.global GetFileVersionInfoA
.global GetFileVersionInfoByHandle
.global GetFileVersionInfoExA
.global GetFileVersionInfoExW
.global GetFileVersionInfoSizeA
.global GetFileVersionInfoSizeExA
.global GetFileVersionInfoSizeExW
.global GetFileVersionInfoSizeW
.global GetFileVersionInfoW
.global VerFindFileA
.global VerFindFileW
.global VerInstallFileA
.global VerInstallFileW
.global VerLanguageNameA
.global VerLanguageNameW
.global VerQueryValueA
.global VerQueryValueW

GetFileVersionInfoA:
    jmp *pGetFileVersionInfoA(%rip)

GetFileVersionInfoByHandle:
    jmp *pGetFileVersionInfoByHandle(%rip)

GetFileVersionInfoExA:
    jmp *pGetFileVersionInfoExA(%rip)

GetFileVersionInfoExW:
    jmp *pGetFileVersionInfoExW(%rip)

GetFileVersionInfoSizeA:
    jmp *pGetFileVersionInfoSizeA(%rip)

GetFileVersionInfoSizeExA:
    jmp *pGetFileVersionInfoSizeExA(%rip)

GetFileVersionInfoSizeExW:
    jmp *pGetFileVersionInfoSizeExW(%rip)

GetFileVersionInfoSizeW:
    jmp *pGetFileVersionInfoSizeW(%rip)

GetFileVersionInfoW:
    jmp *pGetFileVersionInfoW(%rip)

VerFindFileA:
    jmp *pVerFindFileA(%rip)

VerFindFileW:
    jmp *pVerFindFileW(%rip)

VerInstallFileA:
    jmp *pVerInstallFileA(%rip)

VerInstallFileW:
    jmp *pVerInstallFileW(%rip)

VerLanguageNameA:
    jmp *pVerLanguageNameA(%rip)

VerLanguageNameW:
    jmp *pVerLanguageNameW(%rip)

VerQueryValueA:
    jmp *pVerQueryValueA(%rip)

VerQueryValueW:
    jmp *pVerQueryValueW(%rip)
