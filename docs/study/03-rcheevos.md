# 03 · Etapa 3: rcheevos ao vivo

Objetivo da etapa: identificar o jogo e testar lógica de conquistas ao vivo, enquanto você joga.

| Subetapa | Conteúdo | Status |
|---|---|---|
| 3.1 | Hash do jogo + game ID digitado | ✅ |
| 3.2 | Avaliar lógica a cada frame | ✅ |
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

Por isso o hash não muda se o arquivo for renomeado, nem entre um `.bin/.cue` e outro formato de
imagem *do mesmo disco*: só o executável importa. (Isso é uma propriedade do algoritmo; para ler um
`.chd` a ferramenta ainda precisa de um leitor de CHD, que está em "Later / optional".)

**Por que digitar o game ID.** Descobrir o game ID a partir do hash exige perguntar ao servidor do
RA, e a regra 1 do projeto proíbe falar com o servidor sem aprovação. Então você olha o número na
página do jogo no site (o Twilight Syndrome é o 18238, na URL `/game/18238`) e digita uma vez.

**Testar sem jogo: um CD sintético.** O teste monta, byte a byte, um "CD de PS1" mínimo: o
descritor ISO9660 no setor 16 e o terminador no 17, um diretório raiz com `SYSTEM.CNF` e
`SLPS_123.45`, e um executável inventado. Ele tem as partes do ISO9660 que o leitor do rcheevos
usa (com os números nas duas ordens de bytes, como a norma pede), mas **não é uma imagem ISO9660
completa** (datas, tabelas de caminho e o tamanho do volume ficam zerados). Ele grava um `.bin` + `.cue` numa pasta temporária, e o rcheevos lê como se fosse um
jogo, pelo caminho de verdade. O hash esperado foi calculado **à parte, em Python** (`hashlib.md5`
sobre os mesmos bytes): se a nossa ligação com o rcheevos estivesse errada, os dois não bateriam.
Nenhum dado de jogo real entra no repositório.

**Erros por iterador.** O `rc_hash` aceita funções de log de erro de duas formas: globais
(`rc_hash_init_error_message_callback`, como o RALibretro faz) ou dentro do iterador
(`iterator.callbacks.error_message`). Usamos a segunda, com o `userdata` apontando para a string
de erro: cada cálculo coleta os seus próprios erros. (O `rc_hash` ainda tem estado global: os
leitores de arquivo e de CD configurados com `rc_hash_init_custom_*` entram em todo iterador. Não
configuramos nenhum, então ele usa os padrões.)

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

### Na revisão do Codex (PR #37)

- **Game ID mudando em memória mesmo quando a gravação falhava.** Agora a mudança é feita numa
  cópia e só vale depois de gravada.
- **Jogo trocado pelo mesmo caminho mantinha o hash antigo.** O hash agora é recalculado a cada
  nova sessão de jogo, não só quando o caminho muda.
- **"Permissão negada" aparecia como "File not found".** A função `regular_file_problem` separa os
  três casos: não existe, não é arquivo, não deu para acessar (com o motivo do sistema).
- **Duas afirmações minhas nestas notas estavam fortes demais** ("o hash não muda com `.chd`" e
  "sem estado global"). Foram corrigidas acima. Notas de estudo também precisam estar certas.
- **O meu teste de permissão não testava nada.** O teste de mutação mostrou: com o bug de volta,
  ele só emitia um aviso. Corrigido. Lição: um teste que nunca falha não prova nada; quebrar o
  código de propósito é como se descobre isso.

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

---

## 3.2 · Testar lógica de conquista ao vivo

### O que foi feito

- **`LogicTester`** (`src/devtools/include/cw/devtools/logic_tester.hpp`): uma camada fina sobre o
  `rc_runtime` do rcheevos, o mesmo motor que o RetroAchievements usa para avaliar conquistas.
  Você adiciona uma lógica (texto), e a cada frame emulado ele diz o que aconteceu: ativou,
  disparou, pausou, resetou os hits, mudou o valor medido, ou foi desativada por ler um endereço
  que não existe.
- **Janela View → Achievement Tester:** título + lógica, tabela com o estado de cada conquista
  (Waiting / Active / Primed / Triggered / Disabled), o valor medido, botões Re-arm e Remove, e
  um log de eventos com o número do frame. Quando uma conquista dispara, aparece
  "Achievement triggered: …" sobre o jogo.
- **Nada é desbloqueado nem enviado.** As conquistas existem só na memória do programa (ler e
  gravar o `-User.txt` é o passo 4.2).
- **Com o Beetle PSX e o seu jogo:** "o byte em `0x00E200` mudou" disparou no frame 101; uma
  lógica com endereço fora do PS1 (`0x300000`) foi desativada antes de rodar; uma medida mostrou
  `1 / 255`.

### Conceitos

**Como uma conquista "vive" no rcheevos** (`rc_runtime_types.h`, `RC_TRIGGER_STATE_*`):

| Estado | Significado |
|---|---|
| Waiting | Acabou de ser ativada; **não dispara** até a condição ter sido falsa pelo menos uma vez |
| Active | Pode disparar |
| Primed | Tudo verdadeiro, menos as condições `Trigger` (`T:`): está "a um passo" |
| Paused | Uma condição `PauseIf` está verdadeira |
| Triggered | Disparou |
| Disabled | Lê um endereço que a memória não tem |

**Por que o "Waiting" existe.** Imagine carregar um save state em que a condição já é verdadeira
(por exemplo, "está no capítulo 3"). Sem o Waiting, a conquista dispararia na hora, sem você ter
feito nada. O rcheevos exige ver a condição **falsa** antes, para só disparar quando ela *passa a
ser* verdadeira.

**Uma avaliação por frame emulado, logo depois do frame.** É assim que o RA avalia: o jogo roda
um frame, e então as condições são checadas contra a memória daquele frame. Hit counts
(`.3.` = "verdadeira em 3 frames") contam frames emulados. Por isso o avanço de frame (N) avalia
exatamente um frame, e pausado (P) nada é avaliado.

**O `peek`.** O rcheevos não lê a memória sozinho: ele chama uma função nossa pedindo "1, 2 ou 4
bytes a partir do endereço X", e nós respondemos montando os bytes em little-endian a partir da
`MemoryReader` do 2.1. Os 24 bits são lidos como 4 e mascarados pelo próprio rcheevos.

**Callbacks sem contexto, de novo.** As funções de evento e de validação do `rc_runtime` não
recebem um ponteiro de contexto. A solução é a mesma do 1.2 (`s_active`): um ponteiro
`thread_local` aponta para a lista de eventos só durante a chamada.

**Ao trocar ou recarregar o jogo**, o motor é **reconstruído do zero**: todas as conquistas voltam
para Waiting, os hits são zerados, e os endereços são revalidados contra a memória nova. (Veja na
revisão abaixo por que um simples `rc_runtime_reset` não bastava.)

### Arquivos para ler (nesta ordem)

1. `src/devtools/include/cw/devtools/logic_tester.hpp`: a interface e os comentários.
2. `tests/test_logic_tester.cpp`: cada teste conta uma regra do rcheevos (frame exato, Waiting,
   hits, reset, medida, endereço inválido).
3. `src/devtools/logic_tester.cpp`: `peek`, `collect_event`, `validate_addresses`.
4. `src/frontend/main.cpp`: `sync_achievement_tester`, `record_logic_events` e onde `do_frame` é
   chamado (logo depois de `core.run_frame`).
5. Referência: `third_party/rcheevos/src/rcheevos/runtime.c` (`rc_runtime_do_frame`) e
   `trigger.c`.

### O que aprendemos com a referência

- O `rc_runtime` faz todo o trabalho de avaliação (o RALibretro entrega isso ao RAIntegration,
  que usa o `rc_client`, a camada que também conversa com o servidor; nós usamos só o motor,
  sem servidor).
- Reativar uma conquista com a mesma lógica não cria outra: o rcheevos compara um checksum da
  lógica e apenas a reseta (`rc_runtime_activate_achievement`). É assim que o Re-arm funciona.

### Na revisão do Codex (PR #41)

- **Endereço inválido "grudava" depois de trocar de jogo.** Quando um endereço não pode ser lido,
  o rcheevos marca a leitura daquele endereço como "não ler mais" (`RC_VALUE_TYPE_NONE`, em
  `runtime.c`). O `rc_runtime_reset` zera os hits, mas **não desfaz essa marca**. Resultado: num
  jogo em que o endereço existe, a conquista aparecia como Waiting/Active e **nunca mais lia a
  memória**, sem avisar. Correção: ao trocar de jogo, o motor é reconstruído do zero (todas as
  lógicas são lidas de novo); e o Re-arm fica indisponível para conquistas desativadas. Um teste
  reproduzia o bug antes da correção (a conquista não disparava).
- **Leitura no fim da memória:** um valor de 2 ou 4 bytes começando no último byte legível lê os
  bytes que faltam como zero, como o RAIntegration faz; agora há um teste para isso.
- Lição: ler o código da biblioteca (aqui, o que o `rc_runtime_reset` *não* faz) pega bugs que
  nenhum teste "do caminho feliz" mostraria.

### Problemas encontrados

- Faltou um include (`rc_runtime_types.h`, onde ficam os estados e a estrutura completa do
  `rc_trigger_t`); o compilador apontou na hora.
- Teste de mutação: inverter a ordem dos bytes no `peek` (big-endian) derruba 7 testes.

### Para fixar

1. Por que uma conquista recém-adicionada cuja condição já é verdadeira não dispara?
2. O que significa `0xH0010=1.3.`? Em quantos frames ela precisa ser verdadeira?
3. Por que a avaliação acontece depois de cada frame emulado, e não a cada quadro da interface?
4. O que acontece com os hit counts quando você carrega outro jogo? E por quê?
5. Exercício: no jogo, abra o Achievement Tester e adicione `d0xH00E200!=0xH00E200` ("o byte
   mudou desde o frame anterior"). Ele dispara? Aperte Re-arm, pause (P) e avance (N): em qual
   frame dispara de novo?
