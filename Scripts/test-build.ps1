# Run build-script routing checks without an Unreal installation or game window.
param([string]$BuildScript = (Join-Path $PSScriptRoot "build.ps1"))

$ErrorActionPreference = "Stop"
$FixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ("Arcweave build test " + [guid]::NewGuid().ToString("N"))
$ProjectRoot = Join-Path $FixtureRoot "Sample project"
$EngineRoot = Join-Path $FixtureRoot "Fake engine"
$FixtureExe = Join-Path $FixtureRoot "Fixture.exe"
$PackageArguments = Join-Path $FixtureRoot "package-arguments.txt"
$PowerShell = Join-Path $PSHOME "powershell.exe"
$FixtureScript = Join-Path $ProjectRoot "Scripts\build.ps1"

function Assert-Contains($Values, [string]$Expected) {
    if ($Values -notcontains $Expected) { throw "Missing argument: $Expected" }
}

function Invoke-FixtureBuild([string]$Task, [string]$OutputDirectory) {
    $Arguments = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $FixtureScript,
        "-Task", $Task, "-EngineRoot", $EngineRoot)
    if ($Task -eq "WorldTest") { $Arguments += "-Packaged" }
    if ($OutputDirectory) { $Arguments += @("-OutputDirectory", $OutputDirectory) }
    & $PowerShell @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "$Task failed with exit code $LASTEXITCODE." }
}

try {
    New-Item -ItemType Directory -Path (Split-Path $FixtureScript), (Join-Path $EngineRoot "Engine\Build\BatchFiles"), (Join-Path $EngineRoot "Engine\Content\Automation") -Force | Out-Null
    Copy-Item $BuildScript $FixtureScript
    Set-Content (Join-Path $ProjectRoot "ArcweaveQuest.uproject") "{}"
    Set-Content (Join-Path $EngineRoot "Engine\Content\Automation\Report-Template.html") "Fixture report template"

    # One small executable records UAT arguments or simulates a successful automation runner.
    Add-Type -OutputAssembly $FixtureExe -OutputType ConsoleApplication -TypeDefinition @'
using System;
using System.IO;
using System.Reflection;

public class ArcweaveBuildFixture
{
    public static int Main(string[] args)
    {
        if (args.Length > 1 && args[0] == "--record")
        {
            string[] forwarded = new string[args.Length - 2];
            Array.Copy(args, 2, forwarded, 0, forwarded.Length);
            File.WriteAllLines(args[1], forwarded);
            return 0;
        }

        string directory = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
        if (File.Exists(Path.Combine(directory, "reject-runner"))) return 90;
        if (Array.IndexOf(args, "-NullRHI") < 0 || Array.IndexOf(args, "-RenderOffscreen") < 0) return 91;
        if (Array.IndexOf(args, "-ExecCmds=Automation RunTests ArcweaveQuest.World") < 0) return 92;
        string reportArgument = Array.Find(args, value => value.StartsWith("-ReportExportPath="));
        string report = reportArgument.Substring("-ReportExportPath=".Length);
        Directory.CreateDirectory(report);
        File.WriteAllText(Path.Combine(report, "index.json"),
            "{\"succeeded\":1,\"succeededWithWarnings\":0,\"failed\":0,\"notRun\":0}");
        File.WriteAllText(Path.Combine(directory, "runner-invoked"), "yes");
        return 0;
    }
}
'@
    $UAT = '@echo off' + "`r`n" + '"' + $FixtureExe + '" --record "' + $PackageArguments + '" %*' + "`r`nexit /b %ERRORLEVEL%`r`n"
    Set-Content (Join-Path $EngineRoot "Engine\Build\BatchFiles\RunUAT.bat") $UAT -Encoding ASCII

    Push-Location $FixtureRoot
    try {
        $DefaultOutput = Join-Path $ProjectRoot "Builds"
        $Scenarios = @(
            @{ Name = "default"; Argument = ""; Expected = $DefaultOutput },
            @{ Name = "absolute"; Argument = (Join-Path $FixtureRoot "Custom archive"); Expected = (Join-Path $FixtureRoot "Custom archive") },
            @{ Name = "relative"; Argument = ".\Relative archive"; Expected = (Join-Path $FixtureRoot "Relative archive") }
        )
        foreach ($Scenario in $Scenarios) {
            Invoke-FixtureBuild "Package" $Scenario.Argument
            $Recorded = Get-Content $PackageArguments
            Assert-Contains $Recorded "BuildCookRun"
            Assert-Contains $Recorded ("-project=" + (Join-Path $ProjectRoot "ArcweaveQuest.uproject"))
            Assert-Contains $Recorded ("-archivedirectory=" + $Scenario.Expected)
            Assert-Contains $Recorded "-UbtArgs=-CompilerVersion=14.38.33130 -NoHotReloadFromIDE"
            Assert-Contains $Recorded "-AdditionalCookerOptions=-NullRHI -RenderOffscreen"

            $RunnerDirectory = Join-Path $Scenario.Expected "Windows\ArcweaveQuest\Binaries\Win64"
            New-Item -ItemType Directory -Path $RunnerDirectory -Force | Out-Null
            Copy-Item $FixtureExe (Join-Path $RunnerDirectory "ArcweaveQuest.exe")
            Invoke-FixtureBuild "WorldTest" $Scenario.Argument
            if (-not (Test-Path (Join-Path $RunnerDirectory "runner-invoked"))) {
                throw "The $($Scenario.Name) output's executable did not run."
            }
            $Template = Join-Path $Scenario.Expected "Windows\Engine\Content\Automation\Report-Template.html"
            if ((Get-Content $Template -Raw).Trim() -ne "Fixture report template") {
                throw "The report template was not copied to the $($Scenario.Name) output."
            }

            # Later scenarios must not accidentally execute a previous/default package.
            Set-Content (Join-Path $RunnerDirectory "reject-runner") "stale package"
        }
    } finally {
        Pop-Location
    }
    Write-Output "Passed 3 build-script scenarios: default, absolute, and relative output directories. No Unreal process was launched."
} finally {
    Remove-Item $FixtureRoot -Recurse -Force
}
