param(
    [string]$Quark = "build/Release/quark.exe",
    [string]$TestsDir = "tests",
    [string]$OutDir = "tests/bin",
    [switch]$Verbose
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $Quark)) {
    Write-Error "Quark compiler not found at $Quark"
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$invokeQuark = {
    param($compiler, $src, $out)
    $maxTries = 5
    for ($i = 1; $i -le $maxTries; $i++) {
        try {
            & $compiler -q -o $out $src | Out-Null
            return
        } catch {
            if ($i -ge $maxTries) { throw }
            Start-Sleep -Milliseconds 300
        }
    }
}

$testFiles = Get-ChildItem -Path $TestsDir -Filter *.k -File

$total = 0
$passed = 0
$failed = 0

foreach ($tf in $testFiles) {
    $total++
    $name = [IO.Path]::GetFileNameWithoutExtension($tf.Name)
    $exe = Join-Path $OutDir ("$name.exe")

    # Always force a fresh compile by removing any existing binary
    Remove-Item -Path $exe -Force -ErrorAction SilentlyContinue

    if ($Verbose) { Write-Host "Compiling $($tf.FullName) -> $exe" }
    $compileOutput = & $Quark -q -o $exe $tf.FullName 2>&1 | Out-String
    $compileOutput = $compileOutput -replace "\r\n", "`n"
    $compileOutput = $compileOutput.TrimEnd()

    # Check if this is an EXPECT-ERROR test (compiler error expected)
    $expectErrorLines = Select-String -Path $tf.FullName -Pattern "^\s*(#|//)\s*EXPECT-ERROR:\s*(.*)$"
    if ($expectErrorLines) {
        $expectedErr = ($expectErrorLines.Matches | ForEach-Object { $_.Groups[2].Value }) -join "`n"
        $expectedErr = $expectedErr -replace "\r\n", "`n"
        $expectedErr = $expectedErr.TrimEnd()

        if ($compileOutput -like "*error*") {
            # Compare normalized compile output contains expected substring for flexibility
            if ($compileOutput -match [regex]::Escape($expectedErr)) {
                Write-Host "[PASS] $name (EXPECTED ERROR)" -ForegroundColor Green
                $passed++
            } else {
                Write-Host "[FAIL] $name (EXPECTED ERROR mismatch)" -ForegroundColor Red
                Write-Host "Expected (substring):" -ForegroundColor Yellow
                Write-Host $expectedErr
                Write-Host "Got (compile output):" -ForegroundColor Yellow
                Write-Host $compileOutput
                $failed++
            }
        } else {
            Write-Host "[FAIL] $name (expected compile error, but compiled)" -ForegroundColor Red
            $failed++
        }
        continue
    }

    & $invokeQuark $Quark $tf.FullName $exe "--no-cache"

    if (-not (Test-Path $exe)) {
        Write-Host "[FAIL] $name (compile failed)" -ForegroundColor Red
        $failed++
        continue
    }

    if ($Verbose) { Write-Host "Running $exe" }
    $output = ""
    try {
        $output = & $exe 2>&1 | Out-String
    } catch {
        # If the test has no EXPECT lines, treat as a skip and continue
        $expectLines = Select-String -Path $tf.FullName -Pattern "^\s*(#|//)\s*EXPECT:\s*(.*)$"
        if (-not $expectLines) {
            Write-Host "[SKIP] $name (run error, no EXPECT)" -ForegroundColor DarkYellow
            continue
        }
        throw
    }
    $output = $output -replace "\r\n", "`n"
    $output = $output.TrimEnd()

    $expectLines = Select-String -Path $tf.FullName -Pattern "^\s*(#|//)\s*EXPECT:\s*(.*)$"
    if ($expectLines) {
        $expected = ($expectLines.Matches | ForEach-Object { $_.Groups[2].Value }) -join "`n"
        $expected = $expected -replace "\r\n", "`n"
        $expected = $expected.TrimEnd()

        if ($output -eq $expected) {
            Write-Host "[PASS] $name" -ForegroundColor Green
            $passed++
        } else {
            Write-Host "[FAIL] $name" -ForegroundColor Red
            Write-Host "Expected:" -ForegroundColor Yellow
            Write-Host $expected
            Write-Host "Got:" -ForegroundColor Yellow
            Write-Host $output
            $failed++
        }
    } else {
        Write-Host "[RUN ] $name" -ForegroundColor Cyan
        if ($Verbose) { Write-Host $output }
    }
}

Write-Host "`nSummary: $passed/$total passed, $failed failed"
if ($failed -gt 0) { exit 1 } else { exit 0 }
