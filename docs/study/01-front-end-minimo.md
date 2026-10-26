# 01 · Etapa 1: front-end mínimo

Objetivo da etapa: o Twilight Syndrome rodando na ferramenta (core libretro, vídeo, áudio, input,
save states).

| Subetapa | Conteúdo | Status |
|---|---|---|
| 1.1 | Esqueleto: git, submódulos, CMake, janela SDL3 + ImGui | ✅ |
| 1.2 | Carregar o core libretro | ⏳ |
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

### Para fixar

1. Qual a diferença entre `PUBLIC` e `PRIVATE` em `target_link_libraries`? Por que `rcheevos` →
   `libretro_api` é `PUBLIC`?
2. O que aconteceria se `cw_warnings` fosse ligado no alvo `imgui`?
3. Em ImGui, por que `if (ImGui::MenuItem("Quit"))` funciona sem guardar nenhum objeto "menu"?
4. Onde fica o `imgui.ini` se você rodar `XDG_CONFIG_HOME=/tmp/teste ./build/src/frontend/cheevo-workbench`?
   E se `XDG_CONFIG_HOME=pasta-relativa`? (dica: `xdg_base_dir` em `paths.cpp`)
5. Exercício: acrescente na janela "Status" uma linha com o tamanho da janela
   (`ImGui::GetIO().DisplaySize`). Compile e veja funcionar.
