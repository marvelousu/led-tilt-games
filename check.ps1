# ============================================================
#   check.ps1 : MATRIX_EXT 両構成のコンパイル検証
# ============================================================
#   config.h の MATRIX_EXT を 8×32 (未定義) / 16×32 (定義) の
#   両方に書き換えてコンパイルし、片方でも失敗したら exit 1。
#   ・config.h は一時的に書き換えるが、finally で必ず元のバイト列に戻す
#     (arduino-cli の --build-property による -D 追加では config.h 内の
#      #define を打ち消せないため、ファイル書き換え方式を採用)
#   ・arduino-cli は PATH → 既知のインストール場所の順で解決
# ------------------------------------------------------------
$ErrorActionPreference = 'Stop'

$Fqbn     = 'arduino:renesas_uno:nanor4'   # Arduino Nano R4 (README 参照)
$RepoRoot = $PSScriptRoot
$ConfigH  = Join-Path $RepoRoot 'config.h'

# ---- arduino-cli の場所を解決 ----
$cli = $null
$cmd = Get-Command 'arduino-cli' -ErrorAction SilentlyContinue
if ($cmd) {
    $cli = $cmd.Source
} elseif (Test-Path 'C:\Program Files\Arduino CLI\arduino-cli.exe') {
    $cli = 'C:\Program Files\Arduino CLI\arduino-cli.exe'
}
if (-not $cli) {
    Write-Host 'NG: arduino-cli が見つからない (PATH にも C:\Program Files\Arduino CLI にも無い)'
    exit 1
}
Write-Host "arduino-cli : $cli"
Write-Host "FQBN        : $Fqbn"

# ---- config.h の MATRIX_EXT 行を書き換えてコンパイル ----
#   $enabled $true  → '#define MATRIX_EXT'    (16×32)
#   $enabled $false → '// #define MATRIX_EXT' (8×32)
$DefinePattern = '(?m)^[ \t]*(?://[ \t]*)?#define[ \t]+MATRIX_EXT\b[ \t]*(?=\r?$)'

function Invoke-BuildWith([bool]$enabled, [string]$origText) {
    $label = if ($enabled) { 'MATRIX_EXT=1 (16x32)' } else { 'MATRIX_EXT=0 (8x32)' }
    $line  = if ($enabled) { '#define MATRIX_EXT' } else { '// #define MATRIX_EXT' }

    if ($origText -notmatch $DefinePattern) {
        Write-Host "NG: config.h に '#define MATRIX_EXT' 行が見つからない"
        return $false
    }
    $newText = [regex]::Replace($origText, $DefinePattern, $line)
    [System.IO.File]::WriteAllText($ConfigH, $newText, (New-Object System.Text.UTF8Encoding($false)))

    Write-Host "`n==== compile: $label ===="
    & $cli compile -b $Fqbn $RepoRoot
    if ($LASTEXITCODE -ne 0) {
        Write-Host "NG: $label のコンパイル失敗 (exit $LASTEXITCODE)"
        return $false
    }
    Write-Host "OK: $label"
    return $true
}

$origBytes = [System.IO.File]::ReadAllBytes($ConfigH)
$origText  = [System.Text.Encoding]::UTF8.GetString($origBytes)
$ok = $true
try {
    foreach ($enabled in @($false, $true)) {
        if (-not (Invoke-BuildWith $enabled $origText)) { $ok = $false }
    }
} finally {
    # 成否にかかわらず config.h を元のバイト列へ復元
    [System.IO.File]::WriteAllBytes($ConfigH, $origBytes)
}

Write-Host ''
if ($ok) {
    Write-Host 'PASS: MATRIX_EXT 0/1 両構成のコンパイル成功'
    exit 0
} else {
    Write-Host 'FAIL: いずれかの構成でコンパイル失敗'
    exit 1
}
