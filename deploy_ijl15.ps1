#Requires -Version 5.1
<#
.SYNOPSIS
  将编译好的 ijl15.dll 覆盖到北斗客户端目录（BeiDou-Client_1）。

.DESCRIPTION
  供 Post-Build / deploy_ijl15.bat / watch_and_deploy.ps1 调用。
  若目标被 BeiDou.exe 锁定：默认报错退出；传入 -KillClient 或环境变量 DeployKillClient=true 则先结束客户端再复制。

.EXAMPLE
  .\deploy_ijl15.ps1 -DllPath .\out\Release\ijl15.dll
  .\deploy_ijl15.ps1 -DllPath .\out\Release\ijl15.dll -KillClient
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$DllPath,

    [string]$ClientDir = '',

    [string]$ConfigPath = '',

    [string]$DestName = 'ijl15.dll',

    [switch]$KillClient,

    [switch]$NoBackup,

    [switch]$SkipConfig
)

$ErrorActionPreference = 'Stop'

function Write-Info([string]$msg) { Write-Host "[Deploy] $msg" }

# 默认部署目录（可被 -ClientDir / 环境变量覆盖）
if (-not $ClientDir -or [string]::IsNullOrWhiteSpace($ClientDir)) {
    if ($env:BeiDouClientDir) { $ClientDir = $env:BeiDouClientDir }
    else { $ClientDir = 'E:\mxd_soft\2.客户端\083\beidou_client_xiaoye\BeiDou-Client_1' }
}

# 兼容 MSBuild /p:DeployKillClient=true（布尔字符串 / 环境变量）
$doKill = [bool]$KillClient
if (-not $doKill) {
    $envFlag = $env:DeployKillClient
    if ($envFlag -match '^(1|true|yes)$') { $doKill = $true }
}

$DllPath = [System.IO.Path]::GetFullPath($DllPath)
if (-not (Test-Path -LiteralPath $DllPath)) {
    throw "源 DLL 不存在: $DllPath"
}

# --- EXECAVE stub hard gate (2026-08-10) ---
# Guard boot-safety of the main ijl15.dll only: refuse to deploy when the
# EXECAVE bridge @RVA 0xAB30C is gone (stub clobber flashes boot before login).
# Root cause of the 08-08 23:58 full-MSBuild incident + the live A5CA983D flash:
# the .text 0xAB30C slot no longer carried 53 E8.  Never overwrite live from a
# build whose stub is unusable; quarantine the bad artifact and abort instead.
if ($DestName -eq 'ijl15.dll') {
    function Get-RvaFileStub([byte[]]$pe, [uint32]$rva) {
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
            return -1
        }
        $stubBytes = [IO.File]::ReadAllBytes($DllPath)
        $stubOff = Get-RvaFileStub $stubBytes 0xAB30C
        $stubLen = $stubBytes.Length
        $sha16 = (([Security.Cryptography.SHA256]::Create().ComputeHash($stubBytes) | ForEach-Object { $_.ToString('X2') }) -join '').Substring(0, 16)
        if ($stubOff -lt 0 -or $stubBytes[$stubOff] -ne 0x53 -or $stubBytes[$stubOff + 1] -ne 0xE8) {
            $quar = Join-Path $ClientDir '_stub_bad'
            if (-not (Test-Path -LiteralPath $quar)) {
                New-Item -ItemType Directory -Path $quar -Force | Out-Null
            }
            $moved = Join-Path $quar ((Get-Date -Format 'yyyyMMdd_HHmmss') + ".ijl15.dll.sha$sha16.stubBAD")
            Copy-Item -LiteralPath $DllPath -Destination $moved -Force
            throw @"
[StubGate] 拒绝部署: ijl15.dll stub@RVA 0xAB30C 非 53 E8 (EXECAVE clobber)。
  src=$DllPath (len=$stubLen sha16=$sha16)
  已隔离 -> $moved
  请改用 stub-safe 管线重编 (见 _build_native_gr2d.bat / preserve_execave_stub.ps1)。
"@
        }
        Write-Info "StubGate OK: stub@AB30C=53 E8 (len=$stubLen sha16=$sha16)"
        $donorF3 = 'E:\pro\BeiDou-ijl15\golden\ijl15.ADDON_SETITEM_FG_20260808.F3F8D0F2.dll'
        if (Test-Path -LiteralPath $donorF3) {
            $refBytes = [IO.File]::ReadAllBytes($donorF3)
            $refOff = Get-RvaFileStub $refBytes 0xAB30C
            $same = $true
            for ($i = 0; $i -lt 50; $i++) {
                if ($stubBytes[$stubOff + $i] -ne $refBytes[$refOff + $i]) { $same = $false; break }
            }
            if (-not $same) {
                Write-Info "WARN: stub 50B 与 F3F8D0F2 供体不一致 (仅标记位 53 E8 合格，跨编布局可能漂移)"
            }
        }
}

if (-not $ClientDir.EndsWith('\') -and -not $ClientDir.EndsWith('/')) {
    $ClientDir += '\'
}
if (-not (Test-Path -LiteralPath $ClientDir)) {
    New-Item -ItemType Directory -Path $ClientDir -Force | Out-Null
}

$destDll = Join-Path $ClientDir $DestName
$backupDir = Join-Path $ClientDir '_ijl15_backup'

function Test-FileLocked([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { return $false }
    try {
        $fs = [System.IO.File]::Open($path, 'Open', 'ReadWrite', 'None')
        $fs.Close()
        return $false
    } catch {
        return $true
    }
}

function Stop-BeiDouClient {
    $procs = Get-Process -Name 'BeiDou' -ErrorAction SilentlyContinue
    if (-not $procs) {
        Write-Info 'BeiDou.exe 未运行'
        return
    }
    Write-Info "结束 BeiDou.exe (PID: $($procs.Id -join ', ')) ..."
    # Prefer taskkill tree; Stop-Process alone often gets Access Denied on hung clients.
    cmd /c "taskkill /F /IM BeiDou.exe /T >nul 2>&1" | Out-Null
    $procs | Stop-Process -Force -ErrorAction SilentlyContinue
    $deadline = (Get-Date).AddSeconds(10)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 200
        if (-not (Get-Process -Name 'BeiDou' -ErrorAction SilentlyContinue)) { break }
        cmd /c "taskkill /F /IM BeiDou.exe /T >nul 2>&1" | Out-Null
    }
    if (Get-Process -Name 'BeiDou' -ErrorAction SilentlyContinue) {
        # Last resort: elevated helper (UAC may prompt once).
        $helper = Join-Path $env:TEMP 'beidou_force_kill.ps1'
        @(
            'Get-Process BeiDou -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue'
            'cmd /c "taskkill /F /IM BeiDou.exe /T >nul 2>&1"'
            'Start-Sleep -Seconds 1'
        ) | Set-Content -LiteralPath $helper -Encoding ASCII
        try {
            Write-Info '普通权限杀进程失败，尝试提权结束...'
            $p = Start-Process -FilePath powershell.exe -ArgumentList @(
                '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $helper
            ) -Verb RunAs -PassThru -Wait -WindowStyle Hidden
            Write-Info "提权结束 exit=$($p.ExitCode)"
        } catch {
            Write-Info "提权结束失败: $($_.Exception.Message)"
        }
        Start-Sleep -Milliseconds 500
    }
    if (Get-Process -Name 'BeiDou' -ErrorAction SilentlyContinue) {
        throw '无法结束 BeiDou.exe，请在任务管理器手动结束后再部署'
    }
    Start-Sleep -Milliseconds 400
}

if ((Test-FileLocked $destDll) -or (Get-Process -Name 'BeiDou' -ErrorAction SilentlyContinue)) {
    if ($doKill) {
        Stop-BeiDouClient
    } elseif (Test-FileLocked $destDll) {
        throw @"
目标文件被锁定，无法覆盖:
  $destDll
请先关闭 BeiDou.exe，或使用:
  deploy_ijl15.bat /kill
  msbuild ... /p:DeployKillClient=true
"@
    }
}

if (-not $NoBackup -and (Test-Path -LiteralPath $destDll)) {
    if (-not (Test-Path -LiteralPath $backupDir)) {
        New-Item -ItemType Directory -Path $backupDir -Force | Out-Null
    }
    $stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
    $bak = Join-Path $backupDir ("{0}.{1}.bak" -f $DestName, $stamp)
    Copy-Item -LiteralPath $destDll -Destination $bak -Force
    Write-Info "备份 -> $bak"
}

Copy-Item -LiteralPath $DllPath -Destination $destDll -Force
$srcLen = (Get-Item -LiteralPath $DllPath).Length
$dstLen = (Get-Item -LiteralPath $destDll).Length
if ($srcLen -ne $dstLen) {
    throw "复制后大小不一致: src=$srcLen dst=$dstLen"
}
Write-Info "$([IO.Path]::GetFileName($DllPath)) -> $destDll ($dstLen bytes)"

if (-not $SkipConfig -and $ConfigPath -and (Test-Path -LiteralPath $ConfigPath) -and $DestName -eq 'ijl15.dll') {
    $destIni = Join-Path $ClientDir 'config.ini'
    # Preserve client-side feature toggles that operators turn on after deploy
    # (template ezorsia\config.ini defaults pendant2_ui=false for login safety).
    $preservePendant2 = $false
    if (Test-Path -LiteralPath $destIni) {
        $old = Get-Content -LiteralPath $destIni -Raw -ErrorAction SilentlyContinue
        if ($old -match '(?m)^\s*pendant2_ui\s*=\s*true\s*$') { $preservePendant2 = $true }
    }
    Copy-Item -LiteralPath $ConfigPath -Destination $destIni -Force
    if ($preservePendant2) {
        $cfg = Get-Content -LiteralPath $destIni -Raw
        $cfg = $cfg -replace '(?m)^\s*pendant2_ui\s*=\s*false\s*$', 'pendant2_ui=true'
        $utf8NoBom = New-Object System.Text.UTF8Encoding $false
        [IO.File]::WriteAllText($destIni, $cfg, $utf8NoBom)
        Write-Info "config.ini -> $destIni (preserved pendant2_ui=true)"
    } else {
        Write-Info "config.ini -> $destIni"
    }
}

Write-Info '完成'
exit 0
