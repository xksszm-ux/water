param(
    [string]$Python = 'python',
    [string]$CMake = 'cmake'
)
$ErrorActionPreference = 'Stop'
function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
Push-Location $PSScriptRoot
try {
    Invoke-Checked $Python @('-u', 'software_checks/run.py')
    Push-Location 'stm32'
    try {
        foreach ($preset in @('Debug', 'Release')) {
            Invoke-Checked $CMake @('--preset', $preset)
            Invoke-Checked $CMake @('--build', '--preset', $preset)
        }
    } finally { Pop-Location }
    Push-Location 'esp32'
    try { Invoke-Checked 'idf.py' @('build') } finally { Pop-Location }
} finally { Pop-Location }
Write-Output 'Software verification complete. No hardware was accessed.'
