# 01 · Etapa 1: front-end mínimo

Objetivo da etapa: o Twilight Syndrome rodando na ferramenta (core libretro, vídeo, áudio, input,
save states).

| Subetapa | Conteúdo | Status |
|---|---|---|
| 1.1 | Esqueleto: git, submódulos, CMake, janela SDL3 + ImGui | ✅ |
| 1.2 | Carregar o core libretro | ✅ |
| 1.2b | Core de teste sintético (testes sem ROM) | ✅ |
| 1.3 | Vídeo | ✅ |
| 1.4 | Áudio e sincronia | ⏳ |
| 1.5 | Input | ⏳ |
| 1.6 | Save states | ⏳ |
| 1.7 | Menu, abrir arquivos, pausar, avançar frame | ⏳ |

---

## 1.1 · Esqueleto do projeto

### O que foi feito

- `git init` e 4 submódulos em `third_party/`, cada um fixado numa versão:
  imgui `v1.93.0-docking`, rcheevos `v12.5.0`, doctest `v2.5.3`, libretro-common (commit atual).
- Build com CMake em três alvos: `cheevo-workbench` (executável), `cw_devtools` (biblioteca, vazia
  por enquanto) e `cw_tests` (testes).
- Uma janela SDL3 com OpenGL 3.3 e Dear ImGui: menu (File → Quit, View → demo), janela "Status",
  docking ligado. O layout das janelas é salvo em `~/.config/cheevo-workbench/imgui.ini`.
- Dois testes que confirmam que o rcheevos compila e interpreta lógica.

### Conceitos

**Submódulo git.** Um repositório dentro do outro, fixado num commit exato. Quem clona usa
`git clone --recursive` (ou `git submodule update --init`). Vantagem: todo mundo compila com a
*mesma* versão do rcheevos, sem depender do que a distro instalou.

**CMake: alvos (targets).** Cada `add_library` / `add_executable` cria um alvo. Alvos se ligam com
`target_link_libraries`, e cada ligação tem uma visibilidade:

| Palavra | Significado |
|---|---|
| `PRIVATE` | Só este alvo usa (ex.: o executável usa `imgui`) |
| `PUBLIC` | Este alvo usa **e** quem depender dele também herda |
| `INTERFACE` | Este alvo não usa, só repassa (bibliotecas só de headers, como `doctest`) |

Exemplo real: `rcheevos` liga `libretro_api` como `PUBLIC` porque o header `rc_libretro.h` inclui
`libretro.h`. Quem incluir `rc_libretro.h` também precisa achar `libretro.h`.

**`SYSTEM` nos includes.** Marca os headers de terceiros como "do sistema": o compilador não mostra
warnings deles. Assim `-Wall -Wextra` só reclama do **nosso** código.

**`cw_warnings`.** Um alvo `INTERFACE` que só carrega as flags de warning. Ligamos ele apenas nos
nossos alvos, e o código de terceiros compila com as configurações dele.

**XDG Base Directory.** Padrão do Linux para onde guardar arquivos do usuário:
`$XDG_CONFIG_HOME` (padrão `~/.config`) para configuração, `$XDG_DATA_HOME` (`~/.local/share`)
para dados. Nunca escrever na pasta onde o programa foi executado.

**SDL3 + contexto OpenGL.** O SDL cria a janela e um *contexto* OpenGL (o "estado" da GPU ligado
àquela janela). Pedimos OpenGL 3.3 *core*, que funciona em qualquer driver de Linux e é o que cores
com renderização por hardware podem pedir depois.

**Dear ImGui é "immediate mode".** A interface é redesenhada **todo frame** a partir do zero: não
existem objetos "botão" guardados. `if (ImGui::Button("X")) { ... }` desenha o botão *e* diz se ele
foi clicado neste frame. O estado (ex.: janela aberta ou não) fica no nosso código (`UiState`).

**O loop principal** (`main.cpp`), que se repete a cada frame:

1. `SDL_PollEvent`: lê teclado, mouse, fechar janela; repassa ao ImGui.
2. `ImGui::NewFrame()`: começa a interface do frame.
3. Desenha menu e janelas.
4. `ImGui::Render()` + `glClear` + `ImGui_ImplOpenGL3_RenderDrawData`: desenha na GPU.
5. `SDL_GL_SwapWindow`: mostra o frame pronto (vsync limita a ~60 fps).

Nos próximos passos o `retro_run()` do core entra nesse loop, uma vez por frame.

**Docking com `PassthruCentralNode`.** As janelas de dev podem ser encaixadas nas bordas; o centro
fica "vazado" para a imagem do jogo aparecer por baixo (passo 1.3). O multi-janela (janelas fora da
principal) ficou desligado porque funciona mal no Wayland.

**doctest.** Framework de testes num único header. `TEST_CASE("nome") { CHECK(expr); }`. O
`test_main.cpp` só gera o `main()`; `ctest` roda tudo.

### Arquivos para ler (nesta ordem)

1. `CMakeLists.txt`: configuração geral (C++20, warnings, subpastas).
2. `cmake/ThirdParty.cmake`: como cada dependência vira um alvo.
3. `src/frontend/main.cpp`: a janela e o loop.
4. `src/frontend/paths.cpp`: a pasta de configuração XDG.
5. `tests/test_rcheevos.cpp`: os testes.
6. Para comparar: `third_party/imgui/examples/example_sdl3_opengl3/main.cpp` (o exemplo oficial em
   que o nosso `main.cpp` se baseou).

### O que aprendemos com a referência

- O exemplo oficial do ImGui foi a base da janela. Removemos o multi-janela (Wayland) e o código de
  outras plataformas (macOS, Emscripten, OpenGL ES).
- O RALibretro compila só uma parte do rcheevos (`reference/RALibretro/Makefile`): o hash do jogo,
  o `rc_libretro` (mapa de memória) e as informações de console. Avaliar conquistas fica a cargo da
  DLL do RAIntegration, mais um exemplo da divisão emulador × ferramentas de dev.
- Nós compilamos também o runtime (avaliação de lógica, Rich Presence, leaderboards), porque aqui
  o front-end e as ferramentas de dev são o mesmo programa. Deixamos de fora `rapi/` e
  `rc_client*`, as partes que falam com o servidor: assim a regra 1 é garantida no próprio build.

### Problemas encontrados

- **`libretro.h: No such file or directory`** ao compilar `rc_libretro.c`. Causa: o rcheevos
  precisa do header do libretro e não sabia onde achar. Solução: `target_link_libraries(rcheevos
  PUBLIC libretro_api)`.
- **Pastas diferentes no rcheevos.** O `master` (em `reference/`) foi reorganizado depois da
  v12.5.0: `src/runtime/rc_trigger.c` no master é `src/rcheevos/trigger.c` na v12.5.0. Lição:
  sempre estudar a versão que você realmente compila.
- **BIOS quase entrou no git.** O `.gitignore` tinha `*.bin` e `scph*`, mas padrões do
  `.gitignore` **diferenciam maiúsculas** no Linux: `SCPH1001.BIN` escapava dos dois. Solução:
  ignorar a pasta `.utils/` inteira e acrescentar `*.BIN`, `SCPH*` etc. O hook
  `.claude/hooks/guard-bash.sh` também foi reforçado: antes, só bloqueava `git add` que *citasse*
  o arquivo; agora confere o que `git add .` / `git commit -a` realmente pegariam
  (`git ls-files --others --exclude-standard`). Lição: confira com `git status` antes do primeiro
  commit, e `git check-ignore -v <arquivo>` mostra qual regra ignora (ou não) um arquivo.

### Para fixar

1. Qual a diferença entre `PUBLIC` e `PRIVATE` em `target_link_libraries`? Por que `rcheevos` →
   `libretro_api` é `PUBLIC`?
2. O que aconteceria se `cw_warnings` fosse ligado no alvo `imgui`?
3. Em ImGui, por que `if (ImGui::MenuItem("Quit"))` funciona sem guardar nenhum objeto "menu"?
4. Onde fica o `imgui.ini` se você rodar `XDG_CONFIG_HOME=/tmp/teste ./build/src/frontend/cheevo-workbench`?
   E se `XDG_CONFIG_HOME=pasta-relativa`? (dica: `xdg_base_dir` em `paths.cpp`)
5. Exercício: acrescente na janela "Status" uma linha com o tamanho da janela
   (`ImGui::GetIO().DisplaySize`). Compile e veja funcionar.

---

## 1.2 · Carregar o core libretro

### O que foi feito

- Classe `cw::LibretroCore` (`src/frontend/libretro_core.*`): abre o `.so` do core, liga as
  funções `retro_*`, responde às chamadas de *environment*, carrega o jogo e roda um frame por
  vez.
- Classe `cw::CoreOptions` (`src/frontend/core_options.*`): guarda as opções que o core declara
  (ex.: região da BIOS, renderizador) e devolve o valor atual quando o core pergunta.
- Linha de comando (provisória, até o menu do passo 1.7):
  ```sh
  ./build/src/frontend/cheevo-workbench \
      --core /usr/lib/libretro/mednafen_psx_libretro.so \
      --system ".utils/ps1-bios-pacote/ps1 bios" \
      ".utils/Twilight Syndrome - Tansaku-hen (Japan)/Twilight Syndrome - Tansaku-hen (Japan).cue"
  ```
- A janela "Status" mostra core, versão, resolução, fps do core e frames rodados.
- Resultado: o Beetle PSX achou a `scph5500.bin` (BIOS japonesa), carregou o jogo e rodou
  ~60 frames por segundo. Ainda sem imagem, som ou controle (passos 1.3 a 1.5).

### Conceitos

**Um core libretro é uma biblioteca dinâmica (`.so`).** Não é um programa: não tem `main()`. Ele
exporta funções com nomes fixos (`retro_init`, `retro_run`, …) e quem o usa (o *front-end*, nós)
as chama. Abrimos o arquivo em tempo de execução:

- `dlopen(caminho, RTLD_NOW | RTLD_LOCAL)` abre o `.so`. `RTLD_LOCAL` impede que os símbolos
  do core "vazem" e colidam com os de outro core.
- `dlsym(handle, "retro_run")` devolve o endereço da função (como `void*`), que convertemos para
  o tipo certo. O `decltype(&retro_run)` em `Api` pega o tipo exato direto do `libretro.h`.
- `dlclose(handle)` fecha.

**A ordem das chamadas importa** (está no `libretro.h`):

1. `retro_set_environment(cb)`: **antes** do `retro_init`, porque o core já pergunta coisas
   (pasta da BIOS, interface de log) enquanto inicializa.
2. `retro_init()`
3. `retro_set_video_refresh`, `..._audio_sample(_batch)`, `..._input_poll`, `..._input_state`:
   precisam estar definidos antes do primeiro `retro_run`.
4. `retro_load_game(&info)` e depois `retro_get_system_av_info` (resolução, fps, taxa do áudio).
5. A cada frame: `retro_run()`.
6. Para sair: `retro_unload_game()` → `retro_deinit()` → `dlclose()`.

**Environment callback = o core fazendo perguntas ao front-end.** Uma única função recebe um
número de comando (`cmd`) e um ponteiro `data` cujo tipo depende do comando. Exemplos:

| Comando | O core quer… | Respondemos |
|---|---|---|
| `GET_SYSTEM_DIRECTORY` | a pasta da BIOS | o `--system` |
| `GET_SAVE_DIRECTORY` | onde gravar memory cards | `~/.local/share/cheevo-workbench/saves` |
| `GET_LOG_INTERFACE` | uma função para imprimir log | `on_log` (vira `SDL_Log`) |
| `SET_PIXEL_FORMAT` | avisar o formato dos pixels | guardamos (Beetle PSX usa XRGB8888) |
| `SET_CORE_OPTIONS_V2` | declarar suas opções | `CoreOptions` guarda chaves e padrões |
| `GET_VARIABLE` | o valor atual de uma opção | `CoreOptions::get` |

Responder `false` significa "não suporto": o core se adapta (ex.: sem `SET_HW_RENDER`, usa
renderização por software). Por isso é seguro começar com poucos comandos.

**Por que o `s_active` estático?** As funções que passamos ao core são ponteiros de função C
puros, sem um argumento "contexto". Uma função de C não consegue chamar um método de um objeto
C++ sem saber *qual* objeto. Então guardamos um ponteiro estático para o core ativo, e as funções
`on_*` (estáticas) repassam a chamada para ele. Consequência: só um core por vez, o que é o normal
num emulador.

**`need_fullpath`.** Cores de CD (como o de PS1) leem os arquivos sozinhos e só querem o caminho.
Cores de cartucho geralmente pedem o arquivo inteiro na memória; nesse caso lemos o arquivo para
`game_data_` e o mantemos vivo enquanto o jogo roda.

**Funções variádicas (`on_log`).** O log do core funciona como `printf` (`fmt, ...`). Usamos
`va_list` + `vsnprintf` para montar o texto e repassar ao `SDL_Log`. Dica: para ver também as
mensagens de debug, rode com `SDL_LOGGING="app=debug"`.

### Arquivos para ler (nesta ordem)

1. `src/frontend/libretro_core.hpp`: a interface da classe e os comentários.
2. `src/frontend/libretro_core.cpp`: `load`, `load_game`, `unload`, depois `environment`.
3. `src/frontend/core_options.cpp`: os três formatos de opções.
4. `src/frontend/main.cpp`: `parse_args`, `load_from_options` e o `core.run_frame()` no loop.
5. Referência: no `third_party/libretro-common/include/libretro.h`, procure por
   `RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY` e leia o comentário; repita para mais 2 ou 3 comandos.

### O que aprendemos com a referência

Lemos `reference/RALibretro/src/libretro/Core.cpp`:

- A ordem é a mesma: `set_environment` → `init` → `load_game` → callbacks de A/V. Eles definem os
  callbacks de vídeo/áudio só depois do `load_game`; nós logo depois do `init`, o que também é
  válido (só precisam existir antes do primeiro `retro_run`).
- Eles também logam cada comando não suportado **uma vez só**, para não inundar o log; fizemos o
  mesmo com `reported_unsupported_`.
- O RALibretro implementa ~50 comandos (VFS, disco, microfone, rumble…). Nós começamos com o
  mínimo e acrescentamos conforme a necessidade aparecer.

Comandos que o Beetle PSX pediu e ainda **não** tratamos (aparecem com `SDL_LOGGING=app=debug`),
e onde entram:

| Comando | Para quê | Passo |
|---|---|---|
| `SET_MEMORY_MAPS` | descreve a memória do console; essencial para as conquistas | 2.1 |
| `GET_INPUT_BITMASKS` | ler todos os botões de uma vez | 1.5 |
| `SET_SERIALIZATION_QUIRKS` | detalhes de save state | 1.6 |
| `GET_RUMBLE_INTERFACE` | vibração do controle | opcional |
| `SET_DISK_CONTROL_INTERFACE` | trocar de disco (jogos com vários CDs) | opcional |
| `GET_LANGUAGE`, `GET_VFS_INTERFACE`, `GET_LED_INTERFACE`, `GET_PERF_INTERFACE` | extras | opcional |

### Problemas encontrados

- **Caminho relativo da BIOS.** Passar `--system .utils/...` funcionava, mas o core recebia um
  caminho relativo, o que quebra se algo mudar a pasta atual. Agora convertemos com
  `std::filesystem::absolute`.
- **`pkill -f` matou o próprio shell** durante os testes: o padrão procurado aparecia na linha de
  comando do próprio `pkill`. Lição: para achar processos, `ps -eo pid,args | grep '[n]ome'` (o
  `[n]` impede o grep de achar a si mesmo).
- **Sem `HOME`, o app gravava na pasta atual** (achado na revisão do Codex). O `paths.cpp` do 1.1
  caía em `current_path()` quando não havia `XDG_*` nem `HOME`. Agora `config_dir()`/`data_dir()`
  devolvem um caminho **vazio** nesse caso: o layout do ImGui não é salvo e o core recebe `NULL`
  como pasta (o `libretro.h` permite: "no directory defined"). Junto veio outra correção:
  `std::filesystem::create_directories(dir)` sem `error_code` **lança exceção** se falhar; a versão
  com `std::error_code ec` só preenche `ec`.
- **Bug na correção (pego antes de compilar):** a primeira versão usava
  `if (!data.empty() && create_directories(...), !ec)`. O operador vírgula tem a menor precedência
  de todas: aquilo é `(A && B), !ec`, então o `if` passava mesmo com `data` vazio. Lição: evite o
  operador vírgula em condições; escreva em passos.

### Para fixar

1. Por que `retro_set_environment` precisa vir antes de `retro_init`?
2. O que acontece se o environment callback responder `false` para `SET_HW_RENDER`?
3. Por que não dá para passar um método de classe C++ direto como callback do libretro?
4. O que `need_fullpath = true` diz ao front-end? Qual o valor no Beetle PSX, e por quê?
5. Exercício: rode com `SDL_LOGGING="app=debug"` e veja a lista de comandos não suportados.
   Escolha um (ex.: `GET_LANGUAGE`), leia o comentário dele no `libretro.h` e diga o que você
   responderia.

---

## 1.2b · Core de teste sintético

### O que foi feito

- Extraímos `core_options.cpp`, `libretro_core.cpp` e `paths.cpp` para a biblioteca estática
  `cw_frontend`. O executável e `cw_tests` ligam nela; as classes continuam com o mesmo
  comportamento.
- Criamos `cw_test_core`, um módulo C pequeno em `tests/test_core/test_core.c`, que implementa a
  ABI libretro e não depende de ROM ou BIOS. O arquivo de conteúdo sintético dos testes é criado
  na pasta temporária do sistema e removido ao final de cada caso.
- O core oferece uma opção de formato de pixel, anuncia-a pela API v2 e usa `SET_VARIABLES` se o
  front-end informar uma versão de opções abaixo de 2. Ao carregar conteúdo, lê o valor com
  `GET_VARIABLE` e negocia 0RGB1555, XRGB8888 ou RGB565.
- Cada quadro de vídeo tem 64×32 pixels, linhas com 16 bytes de padding e valores determinísticos
  a partir de x, y e do contador de quadros. Consulta `GET_CAN_DUPE` e, quando o front-end permite,
  envia `NULL` a cada quarto quadro para repetir o anterior. Emite 735 quadros de áudio estéreo por
  chamada de `retro_run`.
- O core consulta os 16 botões de `RETRO_DEVICE_JOYPAD` na porta 0 depois de chamar
  `input_poll`. Os 64 KiB de `RETRO_MEMORY_SYSTEM_RAM` guardam o contador em little-endian nos
  bytes 0–3 e a última máscara de 16 bits nos bytes 4–5, também em little-endian (bit *i* indica
  o botão *i*).
- Save state tem tamanho fixo de 65.544 bytes: contador de quadros de 64 bits em little-endian,
  seguido pelos 64 KiB de RAM. `retro_unserialize` rejeita qualquer tamanho diferente.
- Os testes carregam o módulo pelo caminho produzido por CMake, verificam metadados, opção,
  dimensões e contagem de quadros; também cobrem biblioteca ausente, biblioteca que não é core,
  tentativa de carregar conteúdo sem core e unload/reload.

### Conceitos

**Core de teste como `.so`.** `cw_test_core` é compilado como `MODULE`, portanto exporta as funções
`retro_*` que `LibretroCore` procura com `dlsym`. O alvo não é instalado nem distribuído como
emulador; ele existe só para os testes automatizados.

**A biblioteca estática facilita testar o front-end.** Antes, os arquivos do host eram compilados
direto no executável, então os testes não podiam instanciar `cw::LibretroCore`. `cw_frontend`
compartilha a implementação entre o executável e `cw_tests`, sem duplicar fontes nem mudar o
comportamento do host.

**Opções têm versões.** O core pergunta `GET_CORE_OPTIONS_VERSION`; se a resposta for pelo menos 2,
registra a opção com `SET_CORE_OPTIONS_V2`. Caso contrário, usa a lista antiga de
`SET_VARIABLES`. Na hora de carregar o conteúdo, `GET_VARIABLE` devolve a escolha atual e o core
passa o enum correspondente a `SET_PIXEL_FORMAT`.

**Pitch é o tamanho da linha em bytes.** Aqui ele é maior do que `64 × bytes_por_pixel`, porque
cada linha tem 16 bytes que não são pixels. Um front-end que trate pitch como largura em pixels
vai deslocar as linhas. As amostras de áudio também têm uma unidade própria: 735 significa
735 pares estéreo (esquerda, direita), não 735 valores `int16_t` isolados.

**Um frame duplicado ainda conta.** `retro_run` é chamado uma vez por frame de emulação. O core
incrementa o contador mesmo quando envia `NULL` ao callback de vídeo para pedir que o front-end
repita a imagem anterior; áudio e input continuam sendo processados nesse frame.

**Save states precisam ser reprodutíveis.** O estado usa campos serializados explicitamente em
little-endian, sem depender de padding de uma `struct` C ou da arquitetura do compilador. Isso
permite verificar o tamanho e rejeitar buffers truncados ou com tamanho inesperado.

### Arquivos para ler (nesta ordem)

1. `src/frontend/CMakeLists.txt`: como `cw_frontend` compartilha o host entre app e testes.
2. `tests/CMakeLists.txt`: o módulo de teste, caminho no compile definition e ligação com
   `cw_frontend`.
3. `tests/test_core/test_core.c`: callbacks, opção, vídeo, áudio, input, RAM e serialização.
4. `tests/test_libretro_core.cpp`: testes da classe real do front-end com conteúdo temporário.
5. `src/frontend/libretro_core.hpp` e `src/frontend/libretro_core.cpp`: interface exercitada pelos
   testes.
6. `third_party/libretro-common/include/libretro.h`: procure os tipos e comandos citados abaixo.

### O que aprendemos com a referência

Não copiamos código de emuladores: o core sintético foi escrito do zero para ter comportamento
controlado e conteúdo não protegido. Conferimos os contratos diretamente no `libretro.h`:

- `retro_system_info`, `RETRO_API_VERSION` e `retro_game_info` definem identificação, versão da
  ABI e a passagem do conteúdo em memória quando `need_fullpath` é falso.
- `RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION`, `SET_CORE_OPTIONS_V2`, `SET_VARIABLES`,
  `GET_VARIABLE` e `SET_PIXEL_FORMAT` definem a opção e a negociação do formato.
- `RETRO_ENVIRONMENT_GET_CAN_DUPE`, `retro_video_refresh_t`, `retro_pixel_format` e o contrato de
  `retro_run` definem dimensões, pitch e `NULL` como duplicação de quadro.
- `retro_audio_sample_batch_t` define que `frames` conta pares de amostras estéreo.
- `retro_input_poll_t`, `retro_input_state_t`, `RETRO_DEVICE_JOYPAD` e os IDs até
  `RETRO_DEVICE_ID_JOYPAD_R3` definem a leitura dos 16 botões.
- `RETRO_MEMORY_SYSTEM_RAM`, `retro_get_memory_data` e `retro_get_memory_size` definem a região
  exposta pelo core.
- `retro_serialize_size`, `retro_serialize` e `retro_unserialize` definem o contrato de estado;
  o tamanho mínimo exigido pela API é respeitado, e este core escolhe exigir tamanho exato.
- `RETRO_ENVIRONMENT_GET_LOG_INTERFACE` fornece o callback usado para registrar inicialização e
  carregamento.

### Problemas encontrados

- O primeiro build apontou uma função auxiliar de leitura de 32 bits não usada. Ela foi removida;
  o build final foi refeito sem warnings.

### Para fixar

1. Por que `need_fullpath = false` faz o front-end passar `data` e `size` ao core?
2. Se uma linha tem 64 pixels de 16 bits e mais 16 bytes de padding, qual é o pitch em bytes?
3. O que o front-end deve fazer quando `retro_video_refresh` recebe `data == NULL`?
4. Por que o tamanho serializado é `8 + 65.536`, e por que não serializar a `struct` C diretamente?
5. Exercício: altere o core para emitir um quadro duplicado a cada terceiro frame e atualize um
   teste que verifique a contagem de quadros após uma duplicação.

---

## 1.3 · Vídeo

### O que foi feito

- **Conversão de pixels** (`src/frontend/video_frame.*`, na `cw_frontend`): cada frame do core é
  convertido para RGBA8 (4 bytes por pixel, linhas sem padding) a partir dos 3 formatos do
  libretro, respeitando o **pitch**.
- **`LibretroCore`** guarda o último frame (`video_frame()`), com um `serial` que só aumenta
  quando chega imagem nova. Frame duplicado (`data == NULL`) mantém imagem e serial.
  `SET_GEOMETRY` atualiza só largura, altura e proporção. `SET_HW_RENDER` agora é recusado
  explicitamente (com log), e o core cai para renderização por software.
- **`GameView`** (`src/frontend/game_view.*`, só no executável): envia o frame para uma textura
  OpenGL (só quando o serial muda) e desenha a imagem centralizada no nó central do dock, com a
  proporção do core e filtro *nearest* (pixels nítidos).
- **Core de teste** ganhou uma opção para mudar de resolução no frame 3 e passou a pedir
  renderização por hardware no `load_game` (para provar que recusamos).
- **Testes:** conversão com buffers montados à mão (cada formato, padding, entradas inválidas) e,
  com o core de teste, pixels esperados em cada formato, frame duplicado e mudança de geometria.
  13 casos / 224 verificações; limpos no ASan/UBSan.

### Conceitos

**Pitch × largura.** A largura é em *pixels*; o pitch é o tamanho da linha em *bytes* e pode ter
sobra no fim (padding). O endereço do pixel (x, y) é `data + y * pitch + x * bytes_por_pixel`.
Usar `y * largura * bytes_por_pixel` funciona até o dia em que um core manda padding, e aí a imagem
sai "entortada" em diagonal. (O teste de mutação mostrou isso: 6 testes falham.)

**Os três formatos do libretro** (valores de 16/32 bits na ordem nativa da máquina):

| Formato | Bits | Canal de 5 bits → 8 bits |
|---|---|---|
| 0RGB1555 | `0 RRRRR GGGGG BBBBB` (bit 15 ignorado) | `(v << 3) \| (v >> 2)` |
| RGB565 | `RRRRR GGGGGG BBBBB` (verde com 6 bits) | verde: `(v << 2) \| (v >> 4)` |
| XRGB8888 | `XXXXXXXX RRRRRRRR GGGGGGGG BBBBBBBB` | já é 8 bits |

Por que `(v << 3) | (v >> 2)` e não só `v << 3`? Com `v << 3`, o máximo (31) vira 248, e o branco
fica cinza-claro. Repetindo os bits de cima nos de baixo, 31 vira 255.

**Conversão na CPU × direto na GPU.** O RALibretro manda cada formato direto para o OpenGL
(`GL_UNSIGNED_SHORT_5_6_5`, `GL_UNPACK_ROW_LENGTH` para o pitch). É mais rápido, mas só dá para
testar com uma GPU. Nós convertemos para RGBA8 na CPU: um frame de PS1 (320×240) são ~77 mil
pixels, nada para a CPU a 60 fps, e a conversão vira uma função pura, testável sem janela.

**Frame duplicado ("dupe").** Respondemos `GET_CAN_DUPE = true`; então, quando nada mudou, o core
pode passar `NULL` em vez de reenviar a imagem. O `serial` deixa o `GameView` pular o envio para
a GPU nesses frames.

**Proporção (aspect ratio).** O core informa a proporção em que a imagem deve ser *mostrada*, que
pode ser diferente de largura/altura (pixels de console nem sempre são quadrados). Se for `<= 0`,
o `libretro.h` manda usar largura/altura. Desenhamos o maior retângulo com essa proporção que cabe
na área ("letterbox").

**Desenhar atrás das janelas.** `ImGui::GetBackgroundDrawList()` é desenhada antes de todas as
janelas do ImGui. A imagem vai lá, no retângulo do nó central do dock
(`DockBuilderGetCentralNode`, que fica em `imgui_internal.h`: API interna, pode mudar entre versões
do ImGui).

**Recursos OpenGL têm dono.** A textura só existe enquanto existe o contexto OpenGL. Por isso
`GameView::release()` é chamado *antes* de destruir o contexto; chamar `glDeleteTextures` sem
contexto é comportamento indefinido.

### Arquivos para ler (nesta ordem)

1. `src/frontend/video_frame.cpp`: a conversão (curta, vale ler linha a linha).
2. `tests/test_video_frame.cpp`: como os buffers com padding são montados.
3. `src/frontend/libretro_core.cpp`: `on_video_refresh`, `SET_GEOMETRY`, `SET_HW_RENDER`,
   `clear_frame`.
4. `src/frontend/game_view.cpp`: textura, envio e o cálculo do letterbox em `draw`.
5. `src/frontend/main.cpp`: onde `update` e `draw` entram no loop.
6. Referência: `reference/RALibretro/src/components/Video.cpp`, função `refresh` (o caminho
   "direto na GPU").

### O que aprendemos com a referência

- O RALibretro envia o formato nativo para a GPU e usa `GL_UNPACK_ROW_LENGTH` (em pixels) para o
  pitch; escolhemos converter na CPU pela testabilidade (ver acima).
- Ele cria a textura no tamanho **máximo** e desenha só a parte usada. Nós recriamos a textura
  quando a resolução muda, o que é mais simples; mudanças de resolução são raras.

### Problemas encontrados

- **Captura de tela no Wayland/GNOME:** ferramentas externas não conseguem capturar a janela, então
  a verificação visual é sua; os testes cobrem os pixels.
- **Serial reiniciando:** se o serial voltasse a 0 ao carregar outro jogo, o `GameView` podia achar
  que já tinha aquele frame e mostrar a imagem antiga. Por isso o serial nunca é zerado.
- **Teste de mutação:** os testes passaram de primeira, então introduzimos 3 bugs de propósito
  (ignorar o pitch, aceitar hardware rendering, dupe apagando a imagem). Cada um fez testes
  falharem: os testes realmente testam.

### Para fixar

1. Um frame 0RGB1555 de 320 pixels de largura tem pitch 1024. Quantos bytes de padding há por linha?
2. Por que `expand5(31)` precisa dar 255, e quanto daria só com `v << 3`?
3. O que o `GameView` faz quando o core manda um frame duplicado? E por quê?
4. Se o core informar `aspect_ratio = 0` para um frame de 256×224, qual proporção desenhamos?
5. Exercício: na janela Status, mostre também o formato de pixel atual (dica:
   `core.pixel_format()`).
