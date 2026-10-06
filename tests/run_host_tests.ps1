param([string]$Compiler = 'gcc')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDirectory = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force -Path $buildDirectory | Out-Null
$compileFlags = @('-std=c99', '-Wall', '-Wextra', '-Werror', '-pedantic',
    '-D_WIN32_WINNT=0x0600', '-I', "$projectRoot\include",
    '-I', "$projectRoot\port", '-I', "$PSScriptRoot\host",
    '-I', "$projectRoot\examples")

# No FreeRTOS include path: verify the user header and core are independent.
$plainFlags = @('-std=c99', '-Wall', '-Wextra', '-Werror', '-pedantic',
    '-I', "$projectRoot\include", '-I', "$projectRoot\examples")
& $Compiler @plainFlags '-c' "$PSScriptRoot\api_header_check.c" '-o' "$buildDirectory\api_header_check.o"
if ($LASTEXITCODE -ne 0) { throw 'Public API header depends on OS headers' }
& $Compiler @plainFlags '-I' "$projectRoot\port" '-c' "$projectRoot\src\pubsub.c" '-o' "$buildDirectory\pubsub.o"
if ($LASTEXITCODE -ne 0) { throw 'Core depends on OS headers' }

foreach ($configuration in @('tick32-1000hz', 'tick32-100hz', 'tick16-1000hz')) {
    $variantFlags = @()
    if ($configuration -eq 'tick32-100hz') { $variantFlags += '-DHOST_TICK_HZ=100' }
    if ($configuration -eq 'tick16-1000hz') { $variantFlags += '-DHOST_TICK_16=1' }
    $testExecutable = Join-Path $buildDirectory "test_pubsub-$configuration.exe"
    & $Compiler @compileFlags @variantFlags "$buildDirectory\pubsub.o" "$projectRoot\port\pubsub_port.c" "$PSScriptRoot\host\host_rtos.c" "$PSScriptRoot\test_pubsub.c" '-o' $testExecutable
    if ($LASTEXITCODE -ne 0) { throw "Host test compilation failed: $configuration" }
    & $Compiler @compileFlags @variantFlags '-c' "$projectRoot\examples\pubsub_demo.c" '-o' "$buildDirectory\pubsub_demo-$configuration.o"
    if ($LASTEXITCODE -ne 0) { throw "Demo compilation failed: $configuration" }
    Write-Output "Configuration: $configuration"
    & $testExecutable
    if ($LASTEXITCODE -ne 0) { throw "Host tests failed: $configuration" }
}
