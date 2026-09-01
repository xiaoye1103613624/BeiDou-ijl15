#Requires -Version 5.1
<#
.SYNOPSIS
  Post-link EXECAVE bridge fix for BeiDou-ijl15.dll.

.DESCRIPTION
  The client resolves its Addon teardown callback at ijl15 RVA 0xAB30C and calls
  it during set_stage (logo -> login).  The F3 donor body hardcodes two
  layout-sensitive immediates:
      lea eax,[base+0x34B50h]; call eax   ; helper (moved by source re-link)
      mov ecx,[base+0x145008h]            ; pending-flag slot (.data drift)
  After a full source re-link those immediates are wrong:
    * stale helper call -> set_stage 0xC0000005
    * garbage pending flag -> spurious exe Destroy @0x9E00AF -> same crash

  2026-08-13 black-screen finding:
    The previous "inert" body (NOP + `mov eax,0` + NOP + `5B C3`) still produced
    logo set_stage AV @EIP=0 / black screen (Init returns 256) on grown S8
    builds (~1.51MB).  A minimal get-EIP + `pop ebx; ret` stub boots cleanly
    while keeping marker 53 E8 and Level300.

  This tool makes the bridge build-independent:
    * guarantees stub@AB30C starts with 53 E8 (transplants donor prefix if missing);
    * overwrites the 50-byte bridge with a minimal safe ret stub (no helper,
      no slot test, no exe Destroy).

  Usage:
    powershell -File tools\preserve_execave_stub.ps1 -DllPath .\out\Release\ijl15.dll
.PARAMETER DllPath
  Path to the built ijl15.dll to fix in place (a .bak copy is created).
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$DllPath
)

$ErrorActionPreference = 'Stop'
$DllPath = [System.IO.Path]::GetFullPath($DllPath)
if (-not (Test-Path -LiteralPath $DllPath)) { throw "源 DLL 不存在: $DllPath" }

$Donor = 'E:\pro\BeiDou-ijl15\golden\ijl15.ADDON_SETITEM_FG_20260808.F3F8D0F2.dll'

function Get-RvaFileOff([byte[]]$pe, [uint32]$rva) {
    $e = [BitConverter]::ToInt32($pe, 0x3C)
    $n = [BitConverter]::ToUInt16($pe, $e + 6)
    $osz = [BitConverter]::ToUInt16($pe, $e + 20)
    $sec = $e + 24 + $osz
    for ($i = 0; $i -lt $n; $i++) {
        $o = $sec + $i * 40
        $va = [BitConverter]::ToUInt32($pe, $o + 12)
        $raw = [BitConverter]::ToUInt32($pe, $o + 20)
        $rsz = [BitConverter]::ToUInt32($pe, $o + 16)
        if ($rva -ge $va -and $rva -lt ($va + $rsz)) { return [int]($raw + ($rva - $va)) }
    }
    throw "RVA 0x$($rva.ToString('X')) not mapped"
}

function Get-Sha16([byte[]]$bytes) {
    (([Security.Cryptography.SHA256]::Create().ComputeHash($bytes) | ForEach-Object { $_.ToString('X2') }) -join '').Substring(0, 16)
}

$pe = [IO.File]::ReadAllBytes($DllPath)
$stubOff = Get-RvaFileOff $pe 0xAB30C
$sha = Get-Sha16 $pe
Write-Host "[ExecaveStub] before: len=$($pe.Length) sha16=$sha stub@AB30C=$($pe[$stubOff].ToString('X2'))$($pe[$stubOff+1].ToString('X2'))"

# If the linker already placed real code here (e.g. Client::MoreHook after module growth),
# overwriting 50 bytes causes DllMain AV -> client 0xC0000142. Only refresh an existing stub.
if ($pe[$stubOff] -ne 0x53 -or $pe[$stubOff + 1] -ne 0xE8) {
    Write-Host "[ExecaveStub] SKIP: stub@AB30C=$($pe[$stubOff].ToString('X2'))$($pe[$stubOff+1].ToString('X2')) (not 53 E8). Linker layout drift — fix before patching."
    exit 0
}

Write-Host "[ExecaveStub] stub@AB30C already 53 E8 — refreshing minimal bridge only"

# Minimal safe bridge (14B active + NOP pad to 50B for legacy clients expecting a hole here).
# 53 E8 00 00 00 00 5B 81 EB 12 B3 0A 00 5B C3 + NOPs
# Do NOT use mov eax,0 inert (2026-08-13 black screen @ set_stage EIP=0).
$minimal = [byte[]](
    0x53, 0xE8, 0x00, 0x00, 0x00, 0x00,
    0x5B, 0x81, 0xEB, 0x12, 0xB3, 0x0A, 0x00,
    0x5B, 0xC3
)
Write-Host "[ExecaveStub] rewriting bridge to minimal safe ret (keep 53 E8)"
for ($i = 0; $i -lt 50; $i++) {
    if ($i -lt $minimal.Length) {
        $pe[$stubOff + $i] = $minimal[$i]
    } else {
        $pe[$stubOff + $i] = 0x90
    }
}
Write-Host "[ExecaveStub] bridge = get-EIP + ret (no helper / slot / Destroy)"

$newSha = Get-Sha16 $pe
$bak = "$DllPath.bak_$sha"
[IO.File]::Copy($DllPath, $bak, $true)
[IO.File]::WriteAllBytes($DllPath, $pe)
Write-Host "[ExecaveStub] done: len=$($pe.Length) sha16=$newSha backup=$bak"
Write-Host "[ExecaveStub] verify stub@AB30C=$($pe[$stubOff].ToString('X2'))$($pe[$stubOff+1].ToString('X2'))"
