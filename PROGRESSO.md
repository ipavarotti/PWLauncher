# PWLauncher - Progresso / Handoff

Documento para retomar o trabalho depois. Última atualização: renomeado para **PWLauncher**; UI no estilo da referência; modo de verificação (`/verify`); correção do `.pck` (arquivos de pack não viram mais pasta).

---

## 1. Visão geral

Launcher **unificado** em C++ (Win32 puro + GDI+, sem MFC) para Perfect World / Element Client.
Substitui `launcher.exe` + `patcher.exe`: verifica versão, baixa/valida arquivos e abre o jogo,
com UI em janela em camadas (layered) configurável por skin.

- Projeto: `D:\Launcher\PWLauncher`
- Exe: `D:\Launcher\PWLauncher\build\PWLauncher.exe`
- Fonte de referência analisada: `D:\Launcher\EternalClient-developproject` (MFC, protocolo clássico)
- Launcher de referência (binário .NET): `D:\Launcher\Launcher.exe` (OrientalPW)

## 2. Como compilar

```bat
build.bat
:: ou
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 8
```

Requer CMake + MinGW-w64 (testado: g++ 15.1.0) ou Visual Studio 2019/2022.
As imagens de `assets/` são copiadas para `build/skin/` no pós-build.

## 3. Deploy

O launcher detecta o layout sozinho a partir do diretório do exe:
- exe em `<instalacao>\launcher\` -> raiz = pasta-pai (layout clássico)
- exe em `<instalacao>\`         -> raiz = o próprio diretório do exe (estilo referência)

Os arquivos do jogo vão para `<raiz>\element\`. `config.json` (com `InstallDir`) tem prioridade
sobre essa detecção.

```
<instalacao>\
  launcher\PWLauncher.exe
  launcher\skin\skin.ini + imagens
  config\launcher\version.sw        (versao local, JSON)
  config\element\version.sw         (versao local, classico)
  config\element\newver.sw          (temp)
  patcher\server\updateserver.txt   (opcional; UTF-16)
  patcher\server\pid.ini            (opcional)
  element\elementclient.exe
```

## 4. Configuração embutida (`src/BuildConfig.h`)

| Constante | Valor atual | Descrição |
|---|---|---|
| `kEmbeddedUpdateServer` | `http://172.21.119.132/CPW/` | servidor de atualização embutido |
| `kEmbeddedPid` | `101` | pid usado se `pid.ini` não existir |
| `kStrictPidCheck` | `false` | pid diferente só avisa |
| `kVerifySignature` | `true` | valida assinatura RSA das listas |
| `kEmbeddedServerOverride` | `false` | embutido é só fallback |
| `kPckGuard0/1` | `0xfdfdfeee` / `0xf00dbeef` | guard bytes do `.pck` |
| `kPckMask` (Key1) | `0xA33629E2` | XOR do 1º campo / dwEntryOffset |
| `kPckCheckMask` (XOR) | `0x51E5F551` | XOR do 2º campo |

Ordem de prioridade do servidor: `/updateserver:` > (override embutido) > `updateserver.txt`
> embutido (fallback).

### Chave pública RSA (assinatura das listas)
Em `src/Signature.cpp` (`kPem`) está a chave do servidor:
```
-----BEGIN PUBLIC KEY-----
MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDPMMs3JBXN6UFhsUEH0E2W6/+8
NXmqIl4SXLBxoO7kRSXyybiUxBt46sBDDHIJYFxB6vHaGKmO7B+gkhBOXJjsbQix
YlL5/rBXFKe18JNuJr2+tUYeY2BUjIP0vA0rD1y9DuvQOYxugbwkecwfXdexeeCe
rjv4Nq1hoZHQy83iowIDAQAB
-----END PUBLIC KEY-----
```
Validada contra o `files.md5` do servidor (verify=1).
Chave privada correspondente (guardar em local seguro, NÃO no repositório):
`rsa-private-pem` fornecida pelo usuário.

## 5. Protocolo do servidor (172.21.119.132/CPW/) - CLÁSSICO

Confirmado ao vivo:

| Recurso | Resposta |
|---|---|
| `info/pid` | `101` |
| `element/version` | `32` |
| `element/files.md5` | existe, **assinado** |
| `element/v-<k>.inc` | existe (ex.: `v-31.inc`) |
| `element/element/<token>` | `[4 bytes tamanho LE][zlib]` |
| `versions.json` / `element/manifest.json` | **404** (não usa JSON) |

- Manifesto (`files.md5`): linhas `"# 32"` de cabeçalho, depois `<md5> <token>` e por fim o
  marcador `-----BEGIN ELEMENT SIGNATURE-----` + base64.
- `<md5>` = MD5 da forma **comprimida** `[4B size][zlib]` (validado: `7a755fc3...`).
- Compressão: o CPWCreator usa **zlib nível 9** (config `zlib-compression-level`). A verificação
  local recomprime nesse nível (`CompressionLevel`, padrão 9); confirmado que reproduz o MD5 do
  servidor byte a byte. Sem isso, o launcher rebaixaria tudo.
- Incremental: o arquivo é `element/v-<k>.inc` com `k = versaoAlvo - versaoLocal`
  (cabeçalho `# <local> <alvo> <bytes>`). Ex.: de 146 para 147 -> `v-1.inc`.
- **Só baixa o que mudou**: com a lista incremental + verificação local (nível 9), arquivos
  já presentes e iguais são pulados. Validado: reset da versão local p/ 1 -> `v-146.inc`
  (28 itens), **0 downloads** (todos pulados).
- `<token>` = caminho com cada segmento em Base64 separado por `/` (ex.:
  `/ZGF0YQ==/MjMzNDQ1M190aW1lLnR4dA==` = `data/2334453_time.txt`). Decodificado por `DecodeFileName`.
- **Agrupamento de pasta** (gerado pelo CPWCreator): quando arquivos consecutivos estão na
  MESMA pasta, a linha traz **só o nome** (o prefixo/pasta é herdado da linha anterior). O
  token de download precisa do prefixo reconstruído (`g_prefix`, ex.: `./ZGF0YQ==/` +
  `ZWxlbWVudHMuZGF0YQ==`). Sem isso o launcher pede o token curto e recebe **404**.
- Download: `GET element/element/<tokenComPrefixo>` (o servidor normaliza `./` e `//`).

### Fluxo do launcher
1. Tenta JSON (`versions.json`); se 404, cai no clássico.
2. `info/pid` (comparado com `kEmbeddedPid`; não estrito).
3. `element/version` (tenta `element/`, `patcher/`, raiz).
4. Local `< 1` ou `/fullcheck` -> baixa `element/files.md5` (completo).
   Senão incremental `element/v-<k>.inc`.
5. Valida assinatura (se `kVerifySignature`), aplica cada arquivo (baixa, MD5, unpack).
6. Grava `config\element\version.sw`.

## 6. Protocolo JSON (servidor OrientalPW, referência)

O servidor público `http://191.96.78.99/update/` usa outro protocolo (implementado como
caminho primário; não é o do usuário):
- `versions.json` -> `{"latestVersion":71,...}`
- `element/manifest.json` -> `{"files":[{"path","pathBase64","size","hash","added","revision"}]}`
- `element/element/<pathBase64>` -> `[8 bytes tamanho LE][zlib]` -> inflar
- `hash` = MD5 do arquivo **descomprimido**
- Versão local: `config\launcher\version.sw`

## 7. Estado atual (o que está pronto)

- [x] UI Win32 layered + GDI+ no estilo do launcher de referência: 800x500, `img/bg.png`
      (ou gradiente), barra de título (logo + Site/Painel/Discord/Verificar arquivos/
      Verificar atualizações + combo de idioma + minimizar/fechar), cards de notícias
      (`news.json`), barra inferior (status/velocidade/ETA/percentual/progresso/versão) + JOGAR.
- [x] Botões da UI ligados: Jogar, Verificar arquivos, Verificar atualizações, links, idioma.
- [x] Protocolo clássico completo (pid, version, files.md5/inc, assinatura, MD5, unpack).
- [x] Protocolo JSON (versions.json/manifest.json + inflate streaming).
- [x] Assinatura RSA via CryptoAPI (sem OpenSSL) com a chave do usuário.
- [x] Suporte a `.pck` in-pack (Angelica AFilePackage portado) - **não usado** por este servidor.
- [x] zlib embutida; MD5 próprio; parser JSON próprio; Base64/UTF-8 (TransferName).
- [x] Config embutida (`BuildConfig.h`) + arquivos externos opcionais.
- [x] `EnsureParentDir` (corrige criação de arquivo como pasta).
- [x] Build limpo passando (MinGW/CMake).

### Validações feitas contra o servidor real
- `element/version` = 32; `info/pid` = 101.
- Assinatura do `files.md5` = **válida** com a chave do usuário.
- MD5 comprimido de `data/2334453_time.txt` = `7a755fc3f6981c7251bb0a1890cb1440` (bate).
- Unpack `[4B][zlib]` -> 41 bytes OK.
- Parser JSON (servidor de referência) e download+inflate OK.
- **v67 (CPWCreator)**: 28 arquivos, update completo, 0 ausente, ~262 MB no disco
  (`elements.data`, `gshop*.data`, `tasks.data`, fontes). Corrigido o token com prefixo de pasta.

## 7.1 Porte do launcher de referencia (.NET) - motor/comportamento

Referencia: `D:\Launcher\SourceLauncher - JetBrains dotPeek` (WPF/.NET). Foi portado o
**motor e o comportamento**, mantendo o protocolo **classico** do servidor. UI fica para depois.

Implementado em C++ (novos arquivos em `src/`):

| Arquivo | Papel |
|---|---|
| `Localization.{h,cpp}` | strings de `Locales/<lang>.json` (en/es/pt/ru/uk), `Get`/`Format` |
| `NewsService.{h,cpp}` | le `<base>news.json` (`TranslatedString` com fallback por idioma) |
| `PrerequisiteService.{h,cpp}` | VC++ 2015-2022 + DirectX 9; instala de `<installDir>\drivers` |
| `SelfUpdater.{h,cpp}` | self-update adaptado ao classico + modo `--replace` |
| `GuildIconsUpdater.{h,cpp}` | PNG -> DDS (BC1/DXT5) -> grava em `surfaces.pck` |

`config.json` (estilo referencia) agora e lido em `<exeDir>\config.json` e sobrepoe os
arquivos classicos. Campos suportados: `UpdateServer.BaseUrl` (ou `BaseUrl`), `InstallDir`,
`LauncherSelfUpdate`, `Language`, `UseListMode`, `Versions{Launcher,Game,PackDll}`,
`Launch{ExeX86,ArgsX86,ExeX64,ArgsX64,Use64BitOn64OS,RunAsAdmin,CloseLauncherOnStart,WorkingDir}`,
`ClanIcons{Version,VersionUrl,PngUrl,TxtUrl}`, `Links{Website,Profile,Discord}`. E salvo de volta
em `SaveJsonConfig()`.

Comportamentos portados:
- Checagem de espaco em disco (min. 3 GB) e de cliente aberto antes de atualizar.
- `LaunchGame` usa `Launch` (x86/x64, `Use64BitOn64OS`, `RunAsAdmin`, `WorkingDir`, args).
- ETA/velocidade de download no `UpdateStatus` (`speedText`, `etaSeconds`).
- Crash log em `launcher_crash.log` (`SetUnhandledExceptionFilter`).
- Single instance (mutex `PWLauncherExist`) e `--replace` (substitui exe/dll e reinicia).
- Idioma inicial de `config.json.Language`.

Adaptacoes ao servidor classico (nao ha `version.json`):
- **Self-update**: `<base>launcher/version` (texto) + `<base>launcher/<exe>` (+ `.md5` opcional),
  ativado por `LauncherSelfUpdate`. Se 404, ignora.
- **Guild icons**: URLs em `ClanIcons.*` (cache-bust `?r=`); se vazias, nao roda.
- `/<...>:` args: corrigido o parsing de `/updateserver:` (antes comparava 13 vs 14 chars).

### 7.2 Correção do `.pck` (arquivo de pack)

Bug: um arquivo cujo caminho cai num root de pack (ex.: `trees/opencode.sh`) era gravado
**solto**, criando uma pasta `trees\`, porque o launcher **não conseguia abrir o `.pck`**.
Causas e correções (em `PckPackage.cpp` / `PckPackage.h`):

- `dwEntryOffset` (DWORD64) é XORed com o **MASKDWORD de 64 bits** `0xFFFFFFFFA33629E2`;
  antes o XOR usava só 32 bits → offset errado. Corrigido (Open e Save).
- `m_entryOffset` (onde anexar dados novos) deve ser o **offset real das entradas**, não o
  fim do arquivo.
- O trailer varia de tamanho (284 ou 288 — o WinPCK v1.33 adiciona 4 bytes). Agora o
  `FileHeader` é localizado pelos **guard bytes** e o padding é **preservado** no Save.
- `FileEntry.dwOffset` é **32 bits** (registro de 284 bytes), não 64.

Validado: `trees\opencode.sh` foi gravado **dentro de `trees.pck`** (+6473 bytes), sem criar
pasta; o arquivo reabre com 5346 entradas e contém a entrada nova.

> Se você rodou a versão antiga, apague a pasta `element\trees\` criada por engano (o arquivo
> solto) e a pasta errada `D:\Perfect World Client\element`, se existir.

## 8. Pendências / próximos passos1. ~~**Arquivos 404 no servidor**~~ **RESOLVIDO**: `kSkipMissingFiles` (default `true` em
   `BuildConfig.h`) registra `arquivo ausente no servidor (404), ignorando: <caminho>` e
   continua; so aborta em falha de rede/MD5. `elements.data`, `gshop*.data`, `tasks*.data`
   e afins sao pulados. Validado: update classico completo ate o fim (exit 0).
2. Testar o launcher completo no cliente real (fluxo clássico até o fim).
3. ~~**UI igual à referência**~~ **FEITO**: janela 800x500, `img/bg.png`, barra de título com
   nav (Site/Painel/Discord/Verificar arquivos/Verificar atualizações), combo de idioma,
   cards de notícias, barra inferior (status/ETA/%/progresso/versão) e JOGAR.
4. **Modo de verificação** `/verify` (`/repair`) + botão "Verificar arquivos" — **feito**
   (varre `files.md5`, compara local nível 9 e repara faltando/corrompido).
5. (Opcional) Ajustar arte (`img/bg.png`, `img/logo.png`) e textos.
6. (Opcional) Suporte a múltiplos servidores / seleção na UI.

## 9. Mapa dos arquivos (`src/`)

| Arquivo | Papel |
|---|---|
| `main.cpp` | wWinMain, mutex, orquestração, modos `/hide` `/autoclose` |
| `BuildConfig.h` | **configuração embutida** (servidor, pid, chaves pck, assinatura) |
| `Config.{h,cpp}` | lê `update.cfg` / `updateserver.txt` / `pid.ini`; fallback embutido |
| `UpdateEngine.{h,cpp}` | motor: JSON + clássico, download, MD5, deploy, launch |
| `HttpClient.{h,cpp}` | WinINet: download, resume, redirect, `DownloadInflate` |
| `Json.{h,cpp}` | parser JSON mínimo (com BOM) |
| `PckPackage.{h,cpp}` | ler/gravar dentro de `.pck` (Angelica, perfis default/111/181) |
| `Signature.{h,cpp}` | verifica assinatura RSA/MD5 (CryptoAPI) |
| `Md5.{h,cpp}` | MD5 (buffer/arquivo) |
| `Compression.{h,cpp}` | `[size][zlib]` pack/unpack (compress2 nível 1) |
| `Base64.{h,cpp}` | Base64 + `DecodeFileName` (TransferName) |
| `Common.{h,cpp}` | strings, paths, `EnsureParentDir`, `FormatBytes` |
| `Log.{h,cpp}` | log em `config\element\update.log` |
| `LauncherWindow.{h,cpp}` | UI Win32 layered + GDI+, estilo da referência |
| `Localization.{h,cpp}` | strings de idioma (`Locales/*.json`) |
| `NewsService.{h,cpp}` | notícias (`news.json` + `TranslatedString`) |
| `PrerequisiteService.{h,cpp}` | VC++/DirectX (pasta `drivers`) |
| `SelfUpdater.{h,cpp}` | self-update + modo `--replace` |
| `GuildIconsUpdater.{h,cpp}` | guild icons -> `surfaces.pck` (PNG->DDS DXT5) |

## 10. Argumentos de linha de comando

| Arg | Efeito |
|---|---|
| `/fullcheck` | força verificação completa |
| `/verify` (alias `/repair`) | verifica **todos** os arquivos da lista e **repara** os que faltam/corrompidos; ao final registra `Verificação concluída: N ok, M reparado(s), K ausente(s)` |
| `/hide` | modo silencioso (sem UI) |
| `/autoclose` | fecha após atualizar/abrir o jogo |
| `/nopack` | desativa o modo in-pack |
| `/updateserver:<nome ou URL>` | força servidor |
| `/pid:<n>` | sobrepõe o pid |

## 11. Observações

- Este servidor usa **whole-file** (baixa arquivos/packs inteiros); o modo in-pack existe no
  código mas não é exercitado por ele.
- `.pck` suporta versão `0x00020003`; a `0x00020002` (ex.: `Bug.pck`) não é aberta (igual ao
  `AFilePackage` original).
- `verifyLocal` recomprime o local para comparar com o MD5 do manifesto (usa `compress2` nível 1).
- Log: `config\element\update.log` (mostra cada `GET`, status HTTP e layout detectado).
