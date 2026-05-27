param(
    [string]$Version = "1.0.0",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$distDir = Join-Path $repoRoot "dist"
$packageName = "ShortVideoSDK-$Version"
$packageDir = Join-Path $distDir $packageName
$zipPath = Join-Path $distDir "$packageName.zip"

function Copy-IfExists {
    param(
        [Parameter(Mandatory=$true)][string]$Source,
        [Parameter(Mandatory=$true)][string]$Destination
    )

    if (Test-Path $Source) {
        $parent = Split-Path $Destination -Parent
        if ($parent) {
            New-Item -ItemType Directory -Force -Path $parent | Out-Null
        }
        Copy-Item -LiteralPath $Source -Destination $Destination -Recurse -Force
    }
}

New-Item -ItemType Directory -Force -Path $distDir | Out-Null
if (Test-Path $packageDir) {
    Remove-Item -LiteralPath $packageDir -Recurse -Force
}
if (Test-Path $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
New-Item -ItemType Directory -Force -Path $packageDir | Out-Null

if (-not $SkipBuild) {
    Push-Location (Join-Path $repoRoot "android")
    try {
        & .\gradlew.bat :android:assembleRelease
    }
    finally {
        Pop-Location
    }
}

$aarPath = Join-Path $repoRoot "android\android\build\outputs\aar\android-release.aar"
if (-not (Test-Path $aarPath)) {
    throw "Android release AAR was not found at $aarPath. Build first or rerun without -SkipBuild."
}

Copy-IfExists (Join-Path $repoRoot "README.md") (Join-Path $packageDir "README.md")
Copy-IfExists (Join-Path $repoRoot "CHANGELOG.md") (Join-Path $packageDir "CHANGELOG.md")
Copy-IfExists (Join-Path $repoRoot "LICENSE") (Join-Path $packageDir "LICENSE")
Copy-IfExists (Join-Path $repoRoot "docs") (Join-Path $packageDir "docs")
Copy-IfExists (Join-Path $repoRoot "assets") (Join-Path $packageDir "assets")
Copy-IfExists (Join-Path $repoRoot "android\samples") (Join-Path $packageDir "samples")

Copy-IfExists $aarPath (Join-Path $packageDir "android\ShortVideoSDK.aar")
Copy-IfExists (Join-Path $repoRoot "android\consumer-rules.pro") (Join-Path $packageDir "android\consumer-rules.pro")
Copy-IfExists (Join-Path $repoRoot "android\proguard-rules.pro") (Join-Path $packageDir "android\proguard-rules.pro")
Copy-IfExists (Join-Path $repoRoot "docs\dependencies.md") (Join-Path $packageDir "android\dependencies.md")

Copy-IfExists (Join-Path $repoRoot "ios\Classes") (Join-Path $packageDir "ios\Classes")
Copy-IfExists (Join-Path $repoRoot "ios\Package.swift") (Join-Path $packageDir "ios\Package.swift")
Copy-IfExists (Join-Path $repoRoot "ios\VideoSDK.podspec") (Join-Path $packageDir "ios\VideoSDK.podspec")

Compress-Archive -Path (Join-Path $packageDir "*") -DestinationPath $zipPath -Force
Write-Host "Release package created: $zipPath"
