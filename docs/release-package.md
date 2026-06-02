# SDK Release Package

This repository is a source workspace. The public SDK package should be smaller and easier to consume than the workspace itself.

## Recommended Package Shape

```text
ShortVideoSDK-<version>/
├─ README.md
├─ CHANGELOG.md
├─ LICENSE
├─ docs/
├─ android/
│  ├─ ShortVideoSDK.aar
│  ├─ consumer-rules.pro
│  ├─ proguard-rules.pro
│  └─ dependencies.md
├─ ios/
│  ├─ Classes/
│  ├─ Package.swift
│  └─ VideoSDK.podspec
├─ assets/
└─ samples/
   └─ android/
```

## Build

From the repository root:

```powershell
.\tools\package-release.ps1 -Version 1.0.0
```

The script builds `:android:assembleRelease`, collects the Android AAR, copies public documentation and resources, and writes a zip file under `dist/`.

Use this when the Android artifact is already built:

```powershell
.\tools\package-release.ps1 -Version 1.0.0 -SkipBuild
```

## What Belongs In The Release

- Public binaries and integration files: AAR, ProGuard rules, podspec, package manifest, public headers or source bridge files.
- Public resources required at runtime: effects, templates, models, and asset manifests.
- Documentation: integration guide, API notes, changelog, license.
- Samples: small, focused apps that demonstrate integration.

## What Should Stay In The Source Workspace

- Gradle build cache, local SDK paths, `.cxx`, generated build outputs, and temporary release folders.
- Internal test data, CI-only scripts, diagnostics notes, and experimental development documents.
- Full internal module topology unless a customer is expected to build from source.
