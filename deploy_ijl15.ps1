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
