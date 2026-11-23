# WinGet (Windows Package Manager) Integration

This directory contains the manifests and documentation for publishing and maintaining **BatteryHub** in Microsoft's official Windows Package Manager community repository (`microsoft/winget-pkgs`).

---

## 1. Package Metadata & Windows Registry (ARP) Mapping

To ensure WinGet correlates existing installations (made via the standard installer or updated via the built-in auto-updater) without version conflicts or duplicate entries, the package manifests mirror the values written by the Inno Setup installer (`installer/BatteryHub.iss`):

* **PackageIdentifier:** `krisgfx9.WirelessDeviceBatteryMonitor`
* **PackageName:** `BatteryHub`
* **Publisher:** `BatteryHub contributors`
* **InstallerType:** `inno`
* **ProductCode (Inno AppId + `_is1`):** `{7E0B5C2A-3F4D-4C8E-9B61-2D5A8F1C4E93}_is1`
* **DisplayName:** `BatteryHub` (static; does not include version strings)
* **DisplayVersion:** Injected during build time (`AppVersion`)
* **Registry Uninstall Keys:**
  * Per-user (default): `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\{7E0B5C2A-3F4D-4C8E-9B61-2D5A8F1C4E93}_is1`
  * All-users: `HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall\{7E0B5C2A-3F4D-4C8E-9B61-2D5A8F1C4E93}_is1`

### Built-in Auto-Updater Compatibility
The internal auto-updater (`src/update_apply.cpp`) does not perform raw binary replacement; it silently executes the official Inno Setup installer (`BatteryHub-Setup-<version>.exe /WBUPDATE=1 ...`). As a result, the `DisplayVersion` and ARP registry entries are automatically kept in sync whenever an internal update is applied, keeping `winget list` and `winget upgrade` completely synchronized.

---

## 2. One-Time Initial Onboarding to `microsoft/winget-pkgs`

The automated GitHub Action (`winget-releaser`) only *updates* existing packages in `microsoft/winget-pkgs`. The package must be registered manually once:

1. Fork [microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs).
2. Copy the manifest folder `winget/manifests/k/krisgfx9/WirelessDeviceBatteryMonitor/<version>` into your fork under `manifests/k/krisgfx9/WirelessDeviceBatteryMonitor/<version>`.
3. Set the release URL and update `InstallerSha256` to match the published `BatteryHub-Setup-<version>.exe`.
   * Alternatively, use the official tool:
     ```powershell
     wingetcreate new https://github.com/krisgfx9/Wireless-Device-Battery-Monitor/releases/download/v<version>/BatteryHub-Setup-<version>.exe
     ```
4. Validate the manifests locally:
   ```powershell
   winget settings --enable LocalManifestFiles
   winget validate --manifest .\winget\manifests\k\krisgfx9\WirelessDeviceBatteryMonitor\<version>
   ```
5. Submit the pull request to `microsoft/winget-pkgs`. Once approved and merged by Microsoft, the package is live and installable via:
   ```cmd
   winget install krisgfx9.WirelessDeviceBatteryMonitor
   ```

---

## 3. Automated Release Publishing (`.github/workflows/winget.yml`)

Once the initial package is merged in `microsoft/winget-pkgs`, future updates are published automatically upon each GitHub Release.

### Workflow Configuration
* **Workflow file:** `.github/workflows/winget.yml`
* **Trigger:** `release: types: [published]`
* **Repository guard:** `if: github.repository == 'krisgfx9/Wireless-Device-Battery-Monitor'` (prevents accidental runs on third-party forks)
* **Installer filter:** `installers-regex: '\.exe$'` (targets only the setup installer `BatteryHub-Setup-*.exe`, ignoring the portable `.zip` and source archives)
* **Action:** `vedantmgoyal9/winget-releaser@main`

### Repository Secret Setup
For the upstream repository maintainer:
1. Fork `microsoft/winget-pkgs` to your personal GitHub account.
2. Generate a GitHub Personal Access Token (Classic PAT) with the `public_repo` scope.
3. In repository **Settings → Secrets and variables → Actions**, add the token as:
   * **`WINGET_TOKEN`**
