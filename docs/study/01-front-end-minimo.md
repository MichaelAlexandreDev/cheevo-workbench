# 01 · Etapa 1: front-end mínimo

Objetivo da etapa: o Twilight Syndrome rodando na ferramenta (core libretro, vídeo, áudio, input,
save states).

| Subetapa | Conteúdo | Status |
|---|---|---|
| 1.1 | Esqueleto: git, submódulos, CMake, janela SDL3 + ImGui | ✅ |
| 1.2 | Carregar o core libretro | ✅ |
| 1.2b | Core de teste sintético (testes sem ROM) | ⏳ |
| 1.3 | Vídeo | ⏳ |
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
