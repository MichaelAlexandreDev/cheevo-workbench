# 03 · Etapa 3: rcheevos ao vivo

Objetivo da etapa: identificar o jogo e testar lógica de conquistas ao vivo, enquanto você joga.

| Subetapa | Conteúdo | Status |
|---|---|---|
| 3.1 | Hash do jogo + game ID digitado | ✅ |
| 3.2 | Avaliar lógica a cada frame | ⏳ |
| 3.3 | Progresso das conquistas junto com o save state | ⏳ |

---

## 3.1 · Hash do jogo + game ID

### O que foi feito

- **`compute_game_hash`** (`src/devtools/include/cw/devtools/game_hash.hpp`): calcula o hash do
  RetroAchievements de um arquivo de jogo com o `rc_hash` do rcheevos, **offline**.
- **`GameIds`** (`src/frontend/game_ids.*`): guarda o game ID que você digita para cada hash em
  `~/.config/cheevo-workbench/game-ids.txt`. Você digita uma vez; nas próximas vezes ele aparece
  sozinho.
- **Janela Status:** mostra o hash (com botão "Copy") e o campo do game ID. O hash é recalculado
  quando outro jogo é carregado.
- **Issue #34 junto:** core ou jogo inexistente agora dizem "File not found: …" em vez da dica
  enganosa sobre o formato do arquivo.
- **Seu jogo:** hash `ca1c2444593a4dd5ce4946070cec61b3`, calculado em 0,3 ms.

### Conceitos

**O que é o hash e para que serve.** O RA não reconhece um jogo pelo nome do arquivo (cada um dá o
nome que quiser), e sim por uma "impressão digital" do conteúdo: o hash, 32 caracteres
hexadecimais. Cada versão do jogo (japonesa, americana, revisão 1.1…) tem o seu. O site liga cada
hash ao game ID, e cada game ID ao seu set de conquistas. Se o hash do seu arquivo não estiver
listado para o jogo, as conquistas não carregam com ele.

**Como o hash do PS1 é calculado** (`third_party/rcheevos/src/rhash/hash_disc.c`, `rc_hash_psx`):

1. Abre a trilha 1 do disco e lê o sistema de arquivos ISO9660 (o "pendrive" do CD: setor 16
   descreve o disco e aponta para o diretório raiz).
2. Acha o `SYSTEM.CNF` e lê a linha `BOOT = cdrom:\SLPS_xxx.xx;1`: o executável que o PS1 roda ao
   ligar.
3. Hash = **MD5(nome do executável + conteúdo do executável)**, com o tamanho tirado do cabeçalho
   `PS-X EXE` (+ 2048 bytes do próprio cabeçalho).

Por isso o hash não muda entre `.bin/.cue` e `.chd`, nem se o arquivo for renomeado: só o
executável importa.

**Por que digitar o game ID.** Descobrir o game ID a partir do hash exige perguntar ao servidor do
RA, e a regra 1 do projeto proíbe falar com o servidor sem aprovação. Então você olha o número na
página do jogo no site (o Twilight Syndrome é o 18238, na URL `/game/18238`) e digita uma vez.

**Testar sem jogo: um CD sintético.** O teste monta, byte a byte, um "CD de PS1" mínimo: o
descritor ISO9660 no setor 16, um diretório raiz com `SYSTEM.CNF` e `SLPS_123.45`, e um executável
inventado. Ele grava um `.bin` + `.cue` numa pasta temporária, e o rcheevos lê como se fosse um
jogo, pelo caminho de verdade. O hash esperado foi calculado **à parte, em Python** (`hashlib.md5`
sobre os mesmos bytes): se a nossa ligação com o rcheevos estivesse errada, os dois não bateriam.
Nenhum dado de jogo real entra no repositório.

**Callbacks por iterador, não globais.** O `rc_hash` aceita funções de log de erro de duas formas:
globais (`rc_hash_init_error_message_callback`, como o RALibretro faz) ou dentro do iterador
(`iterator.callbacks.error_message`). Usamos a segunda, com o `userdata` apontando para a string
de erro: cada cálculo de hash tem os seus próprios erros, sem estado global.

### Arquivos para ler (nesta ordem)

1. `src/devtools/game_hash.cpp`: curto; o iterador, os callbacks e o `rc_hash_generate`.
2. `tests/test_game_hash.cpp`: `make_ps1_disc` mostra a estrutura mínima de um CD de PS1.
3. `third_party/rcheevos/src/rhash/hash_disc.c`: `rc_cd_find_file_sector` (ISO9660) e
   `rc_hash_psx`.
4. `src/frontend/game_ids.cpp` e, em `main.cpp`, `update_game_identity` / `draw_game_identity`.

### O que aprendemos com a referência

- O RALibretro (`reference/RALibretro/src/Hash.cpp`) usa o mesmo `rc_hash_generate`, mas com
  callbacks globais, um leitor de arquivos próprio (por causa de nomes com acento no Windows),
  suporte a CHD (`HashCHD.cpp`, com a libchdr) e a zip, e conjuntos de discos (`.m3u`). Nós
  começamos com `.cue/.bin/.iso`; CHD está em "Later / optional".

### Problemas encontrados

- A checagem nova de "arquivo não encontrado" quebrou um teste antigo, que usava `libm.so.6` (um
  nome sem caminho, que o `dlopen` procura nas pastas do sistema) para testar "isto não é um core".
  Cores são sempre caminhos de arquivo, então o teste passou a usar um `.so` nosso
  (`tests/test_core/not_a_core.c`).

### Para fixar

1. Por que o RA usa um hash, e não o nome do arquivo, para reconhecer o jogo?
2. O que entra no hash de um jogo de PS1? Mudar o nome do `.bin` muda o hash?
3. Por que calculamos o hash esperado do teste em Python, e não com o próprio rcheevos?
4. Por que o game ID é digitado, e não buscado automaticamente?
5. Exercício: na página do Twilight Syndrome no site do RA, procure a lista de arquivos/hashes
   suportados (no navegador) e confira se `ca1c2444593a4dd5ce4946070cec61b3` está lá.
