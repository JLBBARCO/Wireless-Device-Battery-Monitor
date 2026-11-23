# Integração WinGet (Windows Package Manager)

Este diretório contém os manifestos e orientações para a publicação e manutenção do **BatteryHub** no repositório comunitário oficial da Microsoft (`microsoft/winget-pkgs`).

---

## 1. Identificadores e Mapeamento ARP (Add/Remove Programs)

Para que o WinGet reconheça instalações existentes e permita o upgrade via `winget upgrade` sem criar duplicatas ou conflitos, os metadados do instalador Inno Setup (`installer/BatteryHub.iss`) e do Registro do Windows foram mapeados exatamente da seguinte forma:

* **PackageIdentifier:** `krisgfx9.BatteryHub`
* **InstallerType:** `inno`
* **ProductCode (Inno AppId + `_is1`):** `{7E0B5C2A-3F4D-4C8E-9B61-2D5A8F1C4E93}_is1`
* **DisplayName:** `BatteryHub` (estático, sem versão no nome)
* **Publisher:** `BatteryHub contributors`
* **DisplayVersion:** Definido dinamicamente pelo instalador na compilação (`AppVersion`)
* **Chave no Registro:**
  * Por usuário: `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\{7E0B5C2A-3F4D-4C8E-9B61-2D5A8F1C4E93}_is1`
  * Todos os usuários: `HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall\{7E0B5C2A-3F4D-4C8E-9B61-2D5A8F1C4E93}_is1`

O auto-updater interno do aplicativo (`src/update_apply.cpp`) executa silenciosamente o próprio instalador Inno Setup oficial (`BatteryHub-Setup-<versao>.exe /WBUPDATE=1 ...`), garantindo que a entrada `DisplayVersion` no Registro do Windows seja sempre atualizada automaticamente a cada atualização interna.

---

## 2. Publicação Inicial no `microsoft/winget-pkgs` (Passo Único)

A automação via GitHub Actions (`winget-releaser`) requer que o pacote **já exista** no catálogo do WinGet. Para a primeira versão:

1. Faça um fork de [microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs).
2. Copie a pasta de manifestos deste diretório (`winget/manifests/k/krisgfx9/BatteryHub/<versao>`) para o seu fork, atualizando o `InstallerSha256` e a URL de release para o binário `.exe` publicado.
   * Alternativamente, utilize a ferramenta oficial:
     ```powershell
     wingetcreate new https://github.com/krisgfx9/Wireless-Device-Battery-Monitor/releases/download/v<versao>/BatteryHub-Setup-<versao>.exe
     ```
3. Valide os manifestos localmente:
   ```powershell
   winget settings --enable LocalManifestFiles
   winget validate --manifest .\winget\manifests\k\krisgfx9\BatteryHub\<versao>
   ```
4. Abra o Pull Request no repositório `microsoft/winget-pkgs`. Após a aprovação da Microsoft, o pacote estará disponível para instalação via `winget install krisgfx9.BatteryHub`.

---

## 3. Automação Contínua (`.github/workflows/winget.yml`)

Após a aceitação do pacote no catálogo oficial, toda nova versão publicada em uma **GitHub Release** do repositório upstream será automaticamente enviada ao `microsoft/winget-pkgs`.

### Requisitos para o mantenedor:
1. Criar um fork pessoal de `microsoft/winget-pkgs` na conta do mantenedor.
2. Gerar um GitHub Personal Access Token (Classic PAT) com permissão `public_repo`.
3. Adicionar o token nos segredos do repositório (`Settings > Secrets and variables > Actions`) com o nome:
   * **`WINGET_TOKEN`**

O workflow `.github/workflows/winget.yml` está configurado para:
* Executar apenas em releases publicadas no repositório oficial (`krisgfx9/Wireless-Device-Battery-Monitor`).
* Filtrar unicamente os instaladores executáveis (`.exe`).
* Omitir `fork-user` para autodetectar a conta pelo dono do `WINGET_TOKEN`.
