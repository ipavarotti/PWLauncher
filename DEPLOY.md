# PWLauncher - Deploy / Estrutura de pastas

Guia do que é necessário para rodar o `PWLauncher.exe`.

## Obrigatório

| Item | Observação |
|---|---|
| `PWLauncher.exe` | O launcher. Linkado estaticamente (não precisa de DLLs do MinGW). |
| `element\` | A pasta do jogo (onde ficam os arquivos e o `elementclient.exe`). |

## Opcional (recomendado)

| Item | Sem ele… |
|---|---|
| `Locales\` (`en/es/pt/ru/uk.json`) | Usa o fallback **pt** embutido; não troca de idioma. |
| `img\` (`bg.png`, `logo.png`) | Desenha um gradiente no lugar da arte de fundo/logo. |
| `config.json` | Usa os padrões (servidor embutido, idioma `pt`). |
| `element\drivers\` | Sem a pasta de drivers, apenas registra no log (não instala VC++/DirectX). |
| `patcher\server\updateserver.txt` | Usa o servidor **embutido** no exe (`BuildConfig.h`). |
| `patcher\server\pid.ini` | Usa o pid **embutido** no exe. |

## Criados automaticamente (não precisa criar)

- `element\version.sw` → versão local
- `element\FullList.sw` (+ `newver.sw`, `inc.sw`, `ustate.sw`, `tempfile.dat`) → estado do update
- `element\userdata\accounts.dat` → contas do launcher (login/senha), criptografadas com DPAPI
- `update.log` → **ao lado do executável**
- `_temp\` → downloads, notícias (`news.json`), self-update
- `launcher_crash.log` (só se houver crash)

> Contas: o launcher grava em `element\userdata\accounts.dat` (pasta que o **próprio
> cliente já usa**) e também importa os nomes que o jogo salva em
> `element\userdata\accounts.txt`. Nenhuma pasta nova é criada na raiz.

## Não é mais necessária

- `skin\` — a UI nova **não usa** mais `skin.ini` nem as imagens de botão. Use `img\`.

## Layout

A **raiz** é a pasta que contém o `element\`. Aceita o exe na raiz do cliente **ou** numa
subpasta `launcher\` (o launcher detecta sozinho).

```
<cliente>\
  PWLauncher.exe            (raiz do cliente)   OU   <cliente>\launcher\PWLauncher.exe
  update.log                (auto; ao lado do exe)
  element\                  (obrigatório; recebe version.sw, FullList.sw, ...)
  img\bg.png, img\logo.png  (opcional)
  Locales\pt.json ...       (opcional)
  config.json               (opcional)
  _temp\                    (auto)
```

## Configuração embutida (`src/BuildConfig.h`)

- `kEmbeddedUpdateServer` = `http://172.21.119.132/CPW/` (usado se não houver
  `updateserver.txt` nem `config.json`).
- `kEmbeddedPid` = `101`.
- Chaves do `.pck` e verificação de assinatura.

## `config.json` (opcional) — exemplo

```json
{
  "UpdateServer": { "BaseUrl": "http://172.21.119.132/CPW/" },
  "InstallDir": "element",
  "Language": "pt",
  "LauncherSelfUpdate": false,
  "Versions": { "Launcher": "", "Game": 374, "PackDll": "" },
  "Launch": {
    "ExeX86": "elementclient.exe",
    "ExeX64": "x64\\elementclient_64.exe",
    "Use64BitOn64OS": true,
    "RunAsAdmin": false,
    "CloseLauncherOnStart": true
  },
  "ClanIcons": { "VersionUrl": "", "PngUrl": "", "TxtUrl": "" },
  "Links": { "Website": "", "Profile": "", "Discord": "" }
}
```

## Linha de comando

| Arg | Efeito |
|---|---|
| `/fullcheck` | força verificação completa |
| `/verify` (`/repair`) | verifica todos os arquivos e repara os que faltam/corrompidos |
| `/hide` | modo silencioso (sem UI) |
| `/autoclose` | fecha após atualizar/abrir o jogo |
| `/nopack` | grava tudo como arquivo solto (não usa `.pck`) |
| `/updateserver:<nome ou URL>` | força um servidor |
| `/pid:<n>` | sobrepõe o pid |
