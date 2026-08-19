$ErrorActionPreference = 'Stop'
$projPath = "F:\MXD_dev\BeiDou-ijl15\ezorsia\ezorsia.vcxproj"
[xml]$xml = Get-Content $projPath
$ns = $xml.DocumentElement.NamespaceURI

$debugCond = "'`$(Configuration)|`$(Platform)'=='Debug|Win32'"
$releaseCond = "'`$(Configuration)|`$(Platform)'=='Release|Win32'"

function Ensure-NotUsing($compileEl) {
  foreach ($cond in @($debugCond, $releaseCond)) {
    $existing = @($compileEl.PrecompiledHeader) | Where-Object { $_.Condition -eq $cond }
    if ($existing) {
      foreach ($e in $existing) { $e.InnerText = 'NotUsing' }
    } else {
      $el = $xml.CreateElement('PrecompiledHeader', $ns)
      $el.SetAttribute('Condition', $cond)
      $el.InnerText = 'NotUsing'
      [void]$compileEl.AppendChild($el)
    }
  }
}

$compileGroup = $null
foreach ($ig in $xml.Project.ItemGroup) {
  if ($ig.ClCompile) { $compileGroup = $ig; break }
}
$existing = @{}
foreach ($c in @($compileGroup.ClCompile)) { $existing[$c.Include] = $c }

foreach ($inc in @(
  'equipgrowth\EquipGrowthBridge.cpp',
  'equipgrowth\equipgrowth.cpp',
  'invresize\invresize.cpp'
)) {
  if (-not $existing.ContainsKey($inc)) {
    $el = $xml.CreateElement('ClCompile', $ns)
    $el.SetAttribute('Include', $inc)
    [void]$compileGroup.AppendChild($el)
    $existing[$inc] = $el
    Write-Host "ADD $inc"
  }
}

$moreNotUsing = @(
  'compat\wvs\util.cpp',
  'damagerank\F12test.cpp',
  'damagerank\uiDamageRank.cpp',
  'damagerank\DamageRankInput.cpp',
  'damagerank\DamageRankStage.cpp',
  'equipgrowth\equipgrowth.cpp',
  'invresize\invresize.cpp',
  'setitem\setitem.cpp',
  'setitem\equiptooltip_style.cpp',
  'damageskin\damageskin.cpp',
  'damageskin\damageskinpicker.cpp',
  'beautyshop\beautyshop.cpp',
  'dailycheckin\dailycheckin.cpp',
  'userinfodetail\userinfodetail.cpp'
)

foreach ($c in @($compileGroup.ClCompile)) {
  if ($moreNotUsing -contains $c.Include) {
    Ensure-NotUsing $c
    Write-Host "NotUsing $($c.Include)"
  }
}

foreach ($idg in $xml.Project.ItemDefinitionGroup) {
  if ($idg.Condition -match 'Win32' -and $idg.ClCompile) {
    $cc = $idg.ClCompile
    $defs = [string]$cc.PreprocessorDefinitions
    if ($defs -and $defs -notmatch '_CRT_SECURE_NO_WARNINGS') {
      $cc.PreprocessorDefinitions = '_CRT_SECURE_NO_WARNINGS;' + $defs
    }
    if ($cc.SDLCheck) { $cc.SDLCheck = 'false' }
    else {
      $el = $xml.CreateElement('SDLCheck', $ns)
      $el.InnerText = 'false'
      [void]$cc.AppendChild($el)
    }
    $aid = [string]$cc.AdditionalIncludeDirectories
    if ($aid -and $aid -notmatch 'equipgrowth') {
      $cc.AdditionalIncludeDirectories = '$(ProjectDir)equipgrowth;$(ProjectDir)invresize;' + $aid
    }
    Write-Host "tuned $($idg.Condition)"
  }
}

$xml.Save($projPath)
Write-Host 'saved'
