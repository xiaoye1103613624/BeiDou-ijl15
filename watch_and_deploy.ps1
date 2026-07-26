#Requires -Version 5.1
<#
.SYNOPSIS
  监视 ezorsia 源码变更 → 自动 MSBuild Release → 覆盖客户端 ijl15.dll。

.EXAMPLE
  .\watch_and_deploy.ps1
  .\watch_and_deploy.ps1 -KillClient
#>
[CmdletBinding()]
param(
    [switch]$KillClient,
    [int]$DebounceMs = 1200,
    [string]$Configuration = 'Release',
    [string]$MsBuild = 'D:\software\Microsoft\VisualStudio\Community\MSBuild\Current\Bin\MSBuild.exe'
)

$ErrorActionPreference = 'Stop'
$Root = $PSScriptRoot
$Sln = Join-Path $Root 'ezorsia.sln'
$WatchDir = Join-Path $Root 'ezorsia'
$DeployPs1 = Join-Path $Root 'deploy_ijl15.ps1'
$ClientDir = 'E:\mxd_soft\2.客户端\083\beidou_client_xiaoye\BeiDou-Client_1\'
$ConfigPath = Join-Path $Root 'ezorsia\config.ini'

if (-not (Test-Path -LiteralPath $MsBuild)) { throw "找不到 MSBuild: $MsBuild" }
if (-not (Test-Path -LiteralPath $Sln)) { throw "找不到解决方案: $Sln" }

Write-Host @"
[Watch] 监视: $WatchDir
[Watch] 配置: $Configuration|x86
[Watch] 部署: $ClientDir
[Watch] KillClient: $KillClient
[Watch] 改源码保存后自动编译并覆盖；Ctrl+C 退出
"@

# 跨 runspace 共享（FileSystemWatcher 事件在独立 runspace）
$sync = [hashtable]::Synchronized(@{ LastChange = $null; Building = $false })

function Invoke-BuildDeploy {
    if ($sync.Building) { return }
    $sync.Building = $true
    try {
        Write-Host ""
        Write-Host ("[Watch] {0:HH:mm:ss} 开始编译..." -f (Get-Date))
        & $MsBuild $Sln "/p:Configuration=$Configuration" '/p:Platform=x86' '/m' '/v:minimal' '/p:DeploySkipPostBuild=true'
        if ($LASTEXITCODE -ne 0) {
            Write-Host "[Watch] 编译失败 (exit=$LASTEXITCODE)，等待下次保存" -ForegroundColor Red
            return
        }
        $dll = Join-Path $Root "out\$Configuration\ijl15.dll"
        $args = @{
            DllPath    = $dll
            ClientDir  = $ClientDir
            ConfigPath = $ConfigPath
        }
        if ($KillClient) { $args['KillClient'] = $true }
        & $DeployPs1 @args
        Write-Host ("[Watch] {0:HH:mm:ss} 就绪" -f (Get-Date)) -ForegroundColor Green
    } catch {
        Write-Host "[Watch] 部署失败: $_" -ForegroundColor Red
    } finally {
        $sync.Building = $false
    }
}

$watcher = New-Object System.IO.FileSystemWatcher $WatchDir -Property @{
    IncludeSubdirectories = $true
    NotifyFilter          = [IO.NotifyFilters]'FileName, LastWrite, Size'
    Filter                = '*.*'
    EnableRaisingEvents   = $true
}

$handler = {
    $name = $Event.SourceEventArgs.Name
    if ($name -notmatch '\.(cpp|cxx|cc|c|h|hpp|hxx|inl)$') { return }
    if ($name -match '(^|[\\/])(x64|Win32|\.vs|obj)([\\/]|$)') { return }
    $Event.MessageData.LastChange = Get-Date
    Write-Host ("[Watch] 变更: {0}" -f $name)
}

$subs = @(
    (Register-ObjectEvent $watcher Changed -Action $handler -MessageData $sync),
    (Register-ObjectEvent $watcher Created -Action $handler -MessageData $sync),
    (Register-ObjectEvent $watcher Renamed -Action $handler -MessageData $sync)
)

try {
    while ($true) {
        Start-Sleep -Milliseconds 200
        $last = $sync.LastChange
        if ($null -eq $last) { continue }
        if ($sync.Building) { continue }
        $age = ((Get-Date) - $last).TotalMilliseconds
        if ($age -ge $DebounceMs) {
            $sync.LastChange = $null
            Invoke-BuildDeploy
        }
    }
} finally {
    $watcher.EnableRaisingEvents = $false
    $watcher.Dispose()
    foreach ($s in $subs) {
        Unregister-Event -SourceIdentifier $s.Name -ErrorAction SilentlyContinue
        Remove-Job -Id $s.Id -Force -ErrorAction SilentlyContinue
    }
}
