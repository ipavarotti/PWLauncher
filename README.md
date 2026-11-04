# PWLauncher

Launcher **unificado** em C++ (Win32 puro + GDI+) para Perfect World / Element Client.
Ele faz o papel de `launcher.exe` **e** `patcher.exe` em um único executável:

- verifica a versão no servidor;
- baixa a lista de atualização (completa ou incremental) e valida a **assinatura RSA/MD5**;
- baixa cada arquivo comprimido, valida o **MD5** e descompacta direto no cliente;
- exibe uma UI em janela em camadas (layered window) com skin configurável;
- abre o jogo (`elementclient.exe`) no botão **JOGAR**.

Mantém o **mesmo protocolo de servidor** do projeto original (`files.md5`, `v-N.inc`,
`version`, Base64+UTF-8 nos nomes, `[tamanho LE 4 bytes][zlib]` no corpo dos arquivos),
portanto funciona com o servidor de patch já existente.

---

## Estrutura

```
PWLauncher/
  CMakeLists.txt          build do projeto
  build.bat               atalho de build (Windows)
  src/
    main.cpp              wWinMain, mutex de instancia, orquestracao
    BuildConfig.h         endereco do servidor / pid embutidos no executavel
    Common.{h,cpp}        strings, paths, helpers
    Md5.{h,cpp}           MD5 (arquivo/buffer)
    Base64.{h,cpp}        Base64 + decodificacao de nomes de arquivo do manifesto
    Compression.{h,cpp}   wrapper zlib no formato [size LE][stream]
    Signature.{h,cpp}     verificacao RSA/MD5 (CryptoAPI, sem OpenSSL)
    HttpClient.{h,cpp}    download WinINet (progresso, resume, redirect)
    Config.{h,cpp}        le update.cfg / updateserver.txt / pid.ini
    UpdateEngine.{h,cpp}  motor de atualizacao (JSON + protocolo classico)
    Json.{h,cpp}          parser JSON minimo (versions.json / manifest.json)
    PckPackage.{h,cpp}    leitura/escrita de arquivos dentro dos .pck (Angelica)
    LauncherWindow.{h,cpp} UI Win32 layered + GDI+
    Log.{h,cpp}           log em arquivo
  third_party/zlib/       zlib embutida (compilada estaticamente)
  assets/skin/            imagens de exemplo + skin.ini (copiadas para a saida)
```

---

## Build

Requer CMake 3.15+ e um compilador (MinGW-w64 ou Visual Studio 2019/2022).

### MinGW-w64 (testado)
```bat
build.bat
:: ou
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 8
```

### Visual Studio
```bat
build.bat "Visual Studio 17 2022"
:: gera build/PWLauncher.sln
```
Saida: `build/PWLauncher.exe` (as imagens de `assets/` sao copiadas para `build/skin/`).

---

## Deploy (layout de instalacao)

O executavel deve ficar na pasta `launcher\` da instalacao do jogo:

```
<instalacao>\
  launcher\
    PWLauncher.exe
    skin\
      skin.ini
      main-bg.png
      btn-play.png ... btn-close.png
  config\
    patcher\update.cfg
    element\version.sw          (versao local do cliente; criado automaticamente)
  patcher\server\
    updateserver.txt            (UTF-16: "Nome"  "http://host/update/")
    pid.ini                     ([Version] pid=150)
  element\
    elementclient.exe
    ...                         (arquivos do cliente)
```

O launcher calcula a raiz como o **diretorio pai** de onde esta o `PWLauncher.exe`.

### updateserver.txt
UTF-16LE (com BOM). Cada linha: nome entre aspas + URL entre aspas.
```
"[EN]Patch-1"		"http://192.168.1.225/update/"
```
O nome selecionado vem da chave `UpdateServerSelect` em `config\patcher\update.cfg`.

### update.cfg
```
UpdateServerSelect [EN]Patch-1
SelectServerEveryTime 1
AutoUpdating 1
```

---

## Protocolo JSON (usado pelo servidor OrientalPW)

O launcher tenta primeiro este protocolo; se `<base>versions.json` nao existir, cai no
protocolo classico (secao abaixo).

```
<base>versions.json          -> {"latestVersion":71,"versions":[{"version":71,"type":"full"}]}
<base>element/manifest.json  -> {"version":71,"type":"element","files":[ ... ]}
<base>element/element/<b64>  -> [8 bytes tamanho original LE][zlib] -> inflar -> arquivo
```

Cada item de `files[]`:
```
{ "path": "data/elements.data", "pathBase64": "ZGF0YS9lbGVtZW50cy5kYXRh",
  "size": 23758851, "hash": "<md5 do arquivo DESCOMPRIMIDO>", "added": 2, "revision": 71 }
```

Fluxo:
1. GET `versions.json` -> `latestVersion`; compara com o local (`config\launcher\version.sw`).
2. GET `element/manifest.json` -> lista de arquivos.
3. Para cada arquivo: se o MD5 local == `hash`, pula; senao baixa de
   `element/element/<pathBase64>`, infla, confere o MD5 e grava em `element\<path>`.
4. Grava a versao local.

O download usa streaming (infla direto no arquivo, sem guardar o comprimido em memoria).

---

## Protocolo classico (compativel com o original)

Base = URL do `updateserver.txt` (ex.: `http://host/update/`).

```
<base>info/pid              -> {pid}            (comparado com patcher/server/pid.ini)
<base>element/version       -> {n}              (versao nova)
<base>element/files.md5     -> lista completa, assinada
<base>element/v-<k>.inc     -> lista incremental, assinada
<base>element/element/<tok> -> [tamanho original LE][zlib]  (ou cru se nao comprimir)
```

Cada linha da lista (antes do marcador `-----BEGIN ELEMENT SIGNATURE-----`):
```
<md5> <nomeCodificado>
```
- `<md5>` = MD5 da forma **comprimida** `[size][zlib]` do arquivo.
- No incremental, o 1o caractere do campo MD5 e um flag: `-` = **apagar** o arquivo;
  caso contrario o hash sao os 32 caracteres seguintes.
- `<nomeCodificado>` usa Base64 (alfabeto `A-Za-z0-9+-`) + UTF-8 para trechos nao-ASCII,
  exatamente como o `TransferName` original. O destino e o nome decodificado em `element\`.

O verificador de assinatura usa a **mesma chave publica RSA** do projeto original
(`src/Signature.cpp`) e valida `EVP_md5` + RSA sobre todo o texto antes do marcador.

> Para usar sua propria chave, substitua a PEM em `src/Signature.cpp` e regenere as
> listas assinadas com a chave privada correspondente.

---

## Atualizacao dentro dos .pck (in-pack)

Assim como o patcher original, o launcher **insere/substitui o arquivo dentro do `.pck`**
em vez de gravar um arquivo solto. Isso e feito pelo modulo `PckPackage` (portado do
`AFilePackage` da AngelicaFile), sem depender da DLL do jogo.

- Ao abrir um pacote, o modulo detecta a "algoritmo" pelos *guard bytes* do cabecalho:
  perfil **default** (0xfdfdfeee), **id 111** ou **id 181** (`update`). Assim funciona com
  os `.pck` normais do cliente.
- Suporta a versao de pacote `0x00020003` (a mesma usada pelo cliente/patcher atual).
- Para cada arquivo do manifesto, se o caminho pertence a um pacote conhecido
  (`configs, facedata, gfx, grasses, help\script, interfaces, building, models, sfx,
  shaders, surfaces, textures, trees, litmodels, loddata, script`), ele e gravado dentro
  do `.pck` correspondente; caso contrario, e gravado como arquivo solto.
- O MD5 do manifesto e o MD5 da forma comprimida `[tamanho LE][zlib]`, igual ao original.
- Ao terminar, todos os pacotes abertos sao re-salvos (lista de entradas + cabecalho
  reescritos e arquivo truncado), exatamente como o `AFilePackage::Close`.

Use `/nopack` para desativar o modo in-pack (tudo vira arquivo solto).

## Servidor embutido no executavel

Se voce **nao quer depender de arquivos externos**, edite `src/BuildConfig.h` e recompile:

```cpp
constexpr const wchar_t* kEmbeddedUpdateServer = L"http://192.168.1.225/update/";
constexpr int            kEmbeddedPid          = 150;
constexpr bool           kEmbeddedServerOverride = false;
```

- `kEmbeddedUpdateServer` — endereco compilado dentro do `.exe`. Usado como **fallback**
  quando `patcher\server\updateserver.txt` nao existe/nao resolve. Se
  `kEmbeddedServerOverride = true`, ele tem **prioridade** sobre o arquivo externo.
- `kEmbeddedPid` — pid usado quando `patcher\server\pid.ini` nao existe.
- A linha de comando `/updateserver:` e `/pid:` continuam tendo a maior prioridade.

Ordem de prioridade do servidor: `/updateserver:` > (override embutido) > `updateserver.txt`
(selecionado por `UpdateServerSelect`) > embutido (fallback).

## skin.ini (layout da interface)

Todos os caminhos de imagem sao relativos ao executavel. Coordenadas em pixels.

| Secao | Chaves | Descricao |
|---|---|---|
| `[Window]` | `background`, `width`, `height`, `title` | fundo e tamanho (0 = tamanho da imagem) |
| `[Drag]` | `x,y,w,h` | area para arrastar a janela |
| `[Minimize]` / `[Close]` | `x,y,w,h`, `normal`, `hover`, `click` | botoes da barra de titulo |
| `[Play]` | `x,y,w,h`, `normal`, `hover`, `click`, `disabled` | botao JOGAR |
| `[FileProgress]` / `[TotalProgress]` | `x,y,w,h`, `back`, `fill`, `backcolor`, `fillcolor` | barras de progresso |
| `[Status]` `[CurrentFile]` `[Version]` `[Numbers]` `[FilePercent]` `[TotalPercent]` `[Title]` | `x,y,w,h,font,size,bold,color` | textos dinamicos |
| `[LabelN]` | `text,x,y,w,h,font,size,bold,color` | textos estaticos (rotulos) |
| `[Options]` | `showNumbers`, `showVersion` | exibir numeros/versao |

Cores no formato `R,G,B` ou `A,R,G,B` (0-255). Se uma imagem nao existir, o elemento
cai para um retangulo/ texto desenhado com as cores configuradas.

Os PNGs em `assets/skin/` sao apenas **placeholders** — troque por sua arte.
Fundo recomendado: 960x540 (ou ajuste `width`/`height`).

---

## Linha de comando

| Argumento | Efeito |
|---|---|
| `/fullcheck` | forca verificacao completa (baixa `files.md5`) |
| `/verify` (`/repair`) | verifica todos os arquivos e repara os que faltam/corrompidos |
| `/hide` | modo silencioso (sem UI; ideal para auto-update) |
| `/autoclose` | fecha apos atualizar/abrir o jogo |
| `/updateserver:<nome ou URL>` | forca um servidor especifico |
| `/pid:<n>` | sobrepoe o pid esperado |

---

## Observacoes / limitacoes

- Arquivos que vivem dentro de `.pck` sao atualizados **dentro do pacote** (ver secao
  in-pack). Arquivos fora dos pacotes sao gravados soltos. O manifesto e o servidor
  continuam iguais aos do patcher original.
- Apenas a versao de pacote `0x00020003` e suportada (a mesma do cliente atual). Pacotes
  antigos `0x00020002` (ex.: `Bug.pck`) nao sao abertos, igual ao `AFilePackage` original.
- `verifyLocal` recomprime o arquivo local para comparar com o MD5 do manifesto. Se voce
  gerar as listas com uma zlib diferente e ocorrer divergencia, desative a verificacao
  local (sempre baixa) ou padronize a zlib.
- zlib embutida usa `compress2(level=1)`, o mesmo do `AFilePackage::Compress` original.
