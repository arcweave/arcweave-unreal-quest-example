param(
    [ValidateSet("Editor", "Game", "Test", "WorldTest", "Map", "Package", "Play")][string]$Task = "Editor",
    [string]$EngineRoot = "C:\Program Files\Epic Games\UE_5.6",
    [string]$CompilerVersion = "14.38.33130",
    [string]$TestFilter = "",
    [switch]$Packaged,
    [string]$OutputDirectory = ""
)
$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path $PSScriptRoot -Parent
$Project = Join-Path $ProjectRoot "ArcweaveQuest.uproject"
$Editor = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$Build = Join-Path $EngineRoot "Engine\Build\BatchFiles\Build.bat"
$Stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$Logs = Join-Path $ProjectRoot "Saved\Validation"
New-Item -ItemType Directory -Path $Logs -Force | Out-Null
$Log = Join-Path $Logs "$Task-$Stamp.log"
if ($Task -eq "Editor" -or $Task -eq "Game") {
    $Target = if ($Task -eq "Editor") { "ArcweaveQuestEditor" } else { "ArcweaveQuest" }
    & $Build $Target Win64 Development "-Project=$Project" -WaitMutex -NoHotReloadFromIDE "-CompilerVersion=$CompilerVersion" 2>&1 | Tee-Object -FilePath $Log
    exit $LASTEXITCODE
}
if ($Task -eq "Test" -or $Task -eq "WorldTest") {
    $Report = Join-Path $Logs "Report-$Task-$Stamp"
    if (-not $TestFilter) {
        $TestFilter = if ($Task -eq "WorldTest") { "ArcweaveQuest.World" } else { "ArcweaveQuest.Flow" }
    }
    $TestRunner = $Editor
    $RunnerArguments = @($Project)
    if ($Task -eq "WorldTest") {
        if ($Packaged) {
            $TestRunner = Join-Path $ProjectRoot "Builds\Windows\ArcweaveQuest\Binaries\Win64\ArcweaveQuest.exe"
            $RunnerArguments = @()
            # Packaged games omit this editor report template; supply it for the test report.
            $ReportAssets = Join-Path $ProjectRoot "Builds\Windows\Engine\Content\Automation"
            New-Item -ItemType Directory -Path $ReportAssets -Force | Out-Null
            Copy-Item (Join-Path $EngineRoot "Engine\Content\Automation\Report-Template.html") $ReportAssets -Force
        } else {
            $RunnerArguments += "-game"
        }
    }
    $RunnerArguments += @("-unattended", "-NullRHI", "-RenderOffscreen", "-nosplash", "-nosound", "-nop4", "-UTF8Output", "-NoLiveCoding", "-abslog=$Log", "-ExecCmds=Automation RunTests $TestFilter", "-TestExit=Automation Test Queue Empty", "-ReportExportPath=$Report", "-stdout", "-FullStdOutLogOutput")
    # A pipeline also waits for the packaged Windows GUI executable to exit.
    & $TestRunner @RunnerArguments | Out-Host
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $Summary = Get-Content (Join-Path $Report "index.json") -Raw | ConvertFrom-Json
    $Passed = $Summary.succeeded + $Summary.succeededWithWarnings
    Write-Output "Automation: $Passed passed ($($Summary.succeededWithWarnings) with warnings), $($Summary.failed) failed. Report: $Report"
    if ($Summary.failed -gt 0 -or $Passed -eq 0 -or $Summary.notRun -gt 0) { exit 1 }
    exit 0
}
if ($Task -eq "Map") {
    $MapScript = Join-Path $PSScriptRoot "create-map.py"
    & $Editor $Project -unattended -NullRHI -nosplash -nosound -nop4 -NoLiveCoding -run=pythonscript "-script=$MapScript" "-abslog=$Log" -stdout -FullStdOutLogOutput
    exit $LASTEXITCODE
}
if ($Task -eq "Package") {
    if (-not $OutputDirectory) { $OutputDirectory = Join-Path $ProjectRoot "Builds" }
    & (Join-Path $EngineRoot "Engine\Build\BatchFiles\RunUAT.bat") BuildCookRun "-project=$Project" -noP4 -platform=Win64 -clientconfig=Development -build -cook -stage -pak -archive "-archivedirectory=$OutputDirectory" -utf8output "-UbtArgs=-CompilerVersion=$CompilerVersion" 2>&1 | Tee-Object -FilePath $Log
    exit $LASTEXITCODE
}
if ($Task -eq "Play") {
    & (Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor.exe") $Project -game -windowed -ResX=1440 -ResY=900 -NoLiveCoding
    exit $LASTEXITCODE
}
