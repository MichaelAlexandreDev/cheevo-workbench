# 02 · Etapa 2: memória

Objetivo da etapa: pesquisar a memória do Twilight Syndrome dentro da ferramenta (viewer, busca,
watch list, code notes). É aqui que o trabalho no set começa.

| Subetapa | Conteúdo | Status |
|---|---|---|
| 2.1 | Memória em endereços do RetroAchievements | ✅ |
| 2.2 | Memory viewer e edição de valores | ✅ |
| 2.3 | Busca de memória | ✅ |
| 2.4 | Watch list | ✅ |
| 2.5 | Code notes | ⏳ |

---

## 2.1 · Memória em endereços do RetroAchievements

### O que foi feito

- **Interface `MemoryReader`** (`src/devtools/include/cw/devtools/memory.hpp`): a primeira peça de
  verdade da `cw_devtools`. Ela lê a memória do console em **endereços do RA** e não sabe nada de
  libretro, SDL ou emulador. Junto vieram `read_le` (valores de 1–4 bytes) e `BufferMemory` (um
  vetor de bytes fingindo ser memória de console, para testes).
- **`LibretroMemory`** (`src/frontend/libretro_memory.*`): implementa a interface com o
  `rc_libretro` do rcheevos, a partir do mapa de memória que o core anuncia
  (`SET_MEMORY_MAPS`) ou, sem mapa, do `retro_get_memory_data`.
- **Detecção do console pelo nome do arquivo do core** (`console_for_core`), como no
  `etc/cores.json` do RALibretro: `mednafen_psx_libretro.so` → PlayStation (id 12).
- **`LibretroCore`** guarda uma cópia dos descritores, monta o mapa ao carregar o jogo e o
  **apaga antes de descarregar**. A janela Status mostra o console e as regiões.
- **Com o Beetle PSX e o seu jogo:** console detectado, as 3 regiões disponíveis (inclusive o
  scratchpad), e os 2 MiB lidos pelos endereços do RA idênticos, byte a byte, à RAM do core.

### Conceitos

**Endereço real × endereço do RA.** No PS1 de verdade, a RAM principal fica no endereço
`0x00000000` (ou `0x80000000`, pelo cache), e o scratchpad em `0x1F800000`. O RetroAchievements
define um **espaço de endereços próprio e contínuo** para cada console, para que a lógica das
conquistas seja a mesma em qualquer emulador:

| Endereço do RA | Região | Endereço real no PS1 |
|---|---|---|
| `0x000000`–`0x00FFFF` | Kernel RAM | `0x00000000` |
| `0x010000`–`0x1FFFFF` | System RAM | `0x00010000` |
| `0x200000`–`0x2003FF` | Scratchpad RAM | `0x1F800000` |

(Tabela de `third_party/rcheevos/src/rcheevos/consoleinfo.c`.) Quando você escrever uma conquista
como `0xH01A2B4=5`, o `01A2B4` é um endereço do RA, e é isso que a ferramenta vai mostrar.

**Mapa de memória do core (`SET_MEMORY_MAPS`).** Cada core guarda a memória do jeito dele. Pelo
mapa, o core diz: "o endereço real `0x1F800000`, 1 KiB, está neste ponteiro". O `rc_libretro`
percorre as regiões do RA, procura o descritor que cobre cada endereço real e monta uma lista de
blocos. Sem mapa, ele usa só o `retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM)`, e aí o
scratchpad do PS1 ficaria de fora. É por isso que o mapa importa.

**Por que uma interface (`MemoryReader`).** O viewer, a busca e a avaliação das conquistas vão
ler memória. Se eles dependessem do libretro, só daria para testá-los com um core e um jogo. Com a
interface, eles enxergam "um bloco de memória com endereços do RA", e os testes usam um
`BufferMemory` de 2 MiB montado à mão. A mesma ideia do `devkit` do RAIntegration
(`IEmulatorMemoryContext`), escrita do nosso jeito.

**Ponteiros emprestados e tempo de vida.** As regiões do `rc_libretro` apontam **direto** para a
memória do core. Quando o jogo é descarregado e o `.so` é fechado (`dlclose`), essa memória deixa
de existir. Ler depois disso é ler memória liberada. O teste de mutação mostrou na prática:
sem apagar o mapa no `unload`, o programa caiu com **Segmentation fault**. Por isso o
`LibretroCore` é o dono do mapa e o apaga *antes* de descarregar o core.

**Cópia dos descritores.** O `libretro.h` manda o front-end guardar a própria cópia do mapa "e do
seu conteúdo, incluindo strings". Copiamos o array de descritores **e** as strings `addrspace`
(guardadas em `memory_addrspaces_`, com os ponteiros refeitos para a nossa cópia). A exceção são
os campos `ptr`: eles apontam para a memória do core, que precisa continuar válida durante a
sessão.

**Callback C sem "contexto", de novo.** O `rc_libretro` pede a memória do core por um ponteiro de
função C sem argumento de usuário. É o mesmo problema dos callbacks do libretro (1.2), resolvido
do mesmo jeito: a função estática usa o `s_active`.

### Arquivos para ler (nesta ordem)

1. `src/devtools/include/cw/devtools/memory.hpp`: a interface (curta, e todo o resto da etapa 2
   vai usá-la).
2. `tests/test_memory.cpp`: a interface testada só com buffers.
3. `src/frontend/libretro_memory.cpp`: `console_for_core`, `init`, `read`, `regions`.
4. `src/frontend/libretro_core.cpp`: `SET_MEMORY_MAPS`, `rebuild_memory`, `on_core_memory_info`,
   e o começo de `unload`.
5. `tests/test_libretro_memory.cpp`: o mapa "tipo PS1" do core de teste caindo nos endereços certos.
6. Referência: `third_party/rcheevos/src/rc_libretro.c` (`rc_libretro_memory_init_from_memory_map`)
   e `reference/RAIntegration/src/devkit/context/IEmulatorMemoryContext.hh`.

### O que aprendemos com a referência

- O `rc_libretro` já resolve o mapeamento; o RALibretro o usa do mesmo jeito. Nós só fornecemos
  os descritores e a função que devolve a memória do core.
- O RALibretro identifica o console pelo nome do arquivo do core (`etc/cores.json`); fizemos uma
  tabela pequena, só com os cores de PS1, que dá para ampliar depois.
- O RAIntegration separa "contexto de memória do emulador" das ferramentas que a usam; a nossa
  `MemoryReader` tem o mesmo papel.

### Na revisão do Codex (PR #32)

- **Mapa de um `load_game` rejeitado reaproveitado.** Se o core anunciasse o mapa e depois
  recusasse o jogo, o próximo jogo herdaria aquele mapa (ponteiros possivelmente inválidos). Agora
  os descritores são apagados no início de cada `load_game`.
- **Strings dos descritores não copiadas.** Eu tinha copiado só o array; o contrato pede as
  strings também. Corrigido (e o meu texto acima sobre a cópia estava incompleto).
- **"Disponível" olhava só o primeiro byte.** Uma região mapeada pela metade aparecia como
  disponível. Agora `available` significa "a região inteira pode ser lida".
- **Mapa vazio com ponteiro nulo:** o Codex achou que `nullptr + 0` era comportamento indefinido.
  Em C isso é verdade, mas **em C++ é definido** (o resultado é o próprio ponteiro nulo), e
  `assign(nullptr, nullptr)` é um intervalo vazio válido. Mantivemos uma checagem explícita, que
  deixa a intenção mais clara, mas não era um bug. Revisões também erram; vale conferir.
- Cada correção tem um cenário novo no core de teste (mapa vazio, mapa parcial, load que falha
  depois do mapa, string sobrescrita depois da chamada) e passou no teste de mutação.

### Problemas encontrados

- Nenhum bug novo; o teste de mutação confirmou as duas proteções: apagar o mapa no `unload`
  (sem isso, Segmentation fault) e copiar os descritores (sem isso, o teste do mapa PS1 falha).
- O acessor `system_ram_data()` do 1.5 continua existindo só para testes; o caminho certo agora é
  `memory()`.

### Para fixar

1. Qual a diferença entre o endereço real `0x1F800000` e o endereço do RA `0x200000` no PS1?
2. O que o `rc_libretro` faz quando o core não anuncia um mapa de memória? O que se perde no PS1?
3. Por que o viewer e a busca vão depender de `MemoryReader`, e não do `LibretroCore`?
4. Por que o mapa precisa ser apagado *antes* de descarregar o core, e não depois?
5. Um valor de 2 bytes começando em `0x1FFFFF` vem de onde?
6. Por que, além do array de descritores, precisamos copiar as strings `addrspace`?
7. Exercício: rode o programa com o jogo e veja, na janela Status, as 3 regiões. Depois edite
   `kKnownCores` em `libretro_memory.cpp` removendo o Beetle PSX: o que muda na janela, e por quê?

---

## 2.2 · Memory viewer e edição de valores

### O que foi feito

- A interface `MemoryWriter` foi adicionada separada de `MemoryReader` (`src/devtools/include/cw/devtools/memory.hpp`). A validação exige que todos os bytes estejam no espaço de endereços, disponíveis e dentro de uma única região gravável. Uma falha não altera memória.
- `MemoryRegionInfo` agora informa `writable`. `BufferMemory` aceita regiões sintéticas, incluindo regiões somente leitura e indisponíveis, para testar recusas sem core ou jogo.
- A lógica pura do viewer interpreta bits 0–7, nibble baixo/alto e valores de 8, 16, 24 e 32 bits em little- e big-endian. A codificação aplica os mesmos limites e preserva os outros bits ao editar um bit ou nibble.
- `LibretroMemory` escreve apenas depois de validar a região RA e confirmar que `rc_libretro_memory_find_avail` retorna um bloco contíguo grande o suficiente. O viewer chama a escrita no loop principal depois de `retro_run()` retornar.
- O menu **View → Memory Viewer** abre uma grade hex de 16 bytes por linha, com navegação por endereço e região. `ImGuiListClipper` limita a leitura às linhas visíveis; o painel de seleção mostra todas as interpretações, destaca mudanças entre snapshots e aceita edição hexadecimal por `InputText`.

### Conceitos

**Uma escrita inteira ou nenhuma.** A validação calcula o último endereço com 64 bits, procura uma região que contenha o intervalo completo e, só depois, copia os bytes. Assim, escrever `0xFFFF` a partir do último byte de uma região não modifica o primeiro byte da seguinte. A implementação libretro também respeita o limite do bloco que o rcheevos devolve.

**Os tamanhos são os do rcheevos.** `rc_peek_value` obtém o valor base como little-endian; `rc_transform_memref_value` mascara o tamanho e, para 16/24/32 bits big-endian, inverte os bytes. Bit 0 é o bit menos significativo; nibble baixo ocupa os bits 0–3 e nibble alto, os bits 4–7. A visualização e a edição seguem essa mesma regra.

**Bit e nibble são campos dentro de um byte.** A edição lê o byte atual, troca só a máscara escolhida e escreve o byte resultante. Por exemplo, editar o nibble baixo de `0xA5` para `2` produz `0xA2`, mantendo o nibble alto.

**O viewer não percorre os 2 MiB a cada desenho.** A lista informa ao clipper quantas linhas existem; a memória é lida durante a renderização das linhas visíveis. Cada conjunto visível é comparado ao conjunto anterior, e só os endereços presentes nos dois snapshots com valor diferente recebem destaque. O painel também lê o endereço selecionado para preencher suas interpretações.

**Entrada de texto e teclado do jogo.** O endereço e o valor usam `InputText`. O loop já consulta `io.WantTextInput` antes de atualizar o joypad, então as teclas digitadas nesses campos não são encaminhadas ao jogo.

### Arquivos para ler

1. `src/devtools/include/cw/devtools/memory.hpp` e `src/devtools/memory.cpp`: escrita, interpretações, codificação e comparação dos snapshots.
2. `tests/test_memory.cpp`: tamanhos/endians conhecidos, ida e volta, preservação de bits, recusas e mudança entre snapshots.
3. `src/frontend/libretro_memory.cpp`: tipos das regiões RA e o limite de bloco em `rc_libretro_memory_find_avail`.
4. `tests/test_libretro_memory.cpp`: escrita refletida na RAM do core sintético com mapa PS1 e recusa na fronteira.
5. `src/frontend/main.cpp`: janela, `ImGuiListClipper`, entrada de texto, edição e menu.
6. `third_party/rcheevos/src/rcheevos/memref.c`, `third_party/rcheevos/include/rc_runtime_types.h` e `third_party/rcheevos/include/rc_consoles.h`: tamanhos, transformação endian e `RC_MEMORY_TYPE_READONLY`.
7. `reference/RAIntegration/src/devkit/data/Memory.hh` e `reference/RAIntegration/src/ui/viewmodels/MemoryViewerViewModel.cpp`: comparação de tamanhos e do comportamento do viewer (referência de leitura; nenhum código foi copiado).

### O que aprendemos com a referência

- No rcheevos v12.5.0 compilado aqui, o `peek` de vários bytes entrega um inteiro little-endian; as variantes big-endian são uma transformação posterior. Não basta trocar a ordem dos bytes no desenho da grade: a conversão numérica também precisa corresponder.
- `RC_MEMORY_TYPE_READONLY` é o tipo explícito de região somente leitura. A região só é marcada gravável se estiver completamente disponível e não tiver esse tipo. Para consoles desconhecidos, a memória plana só é gravável quando todos os bytes estão mapeados.
- A RAIntegration confirma os nomes dos tamanhos e trata a edição por valores de memória. Nós usamos uma interface nossa e validação atômica, adequada aos testes com `BufferMemory` e sem dependência de SDL.

### Verificação

- `cmake -S . -B build` e `cmake --build build -j`: passaram sem warnings.
- `ctest --test-dir build --output-on-failure`: passou.
- Build e `ctest` separados com `-fsanitize=address,undefined`: passaram.
- O executável foi iniciado com `SDL_VIDEODRIVER=offscreen`, Beetle PSX, BIOS e o jogo das notas; permaneceu no loop até ser interrompido. Esse smoke test sem janela interativa não permite confirmar cliques/edição visualmente; o roteiro manual está no rascunho do PR.

### Problemas encontrados

- Nenhum erro de build ou teste. A escrita no core sintético confirmou que o endereço RA aponta para a RAM original do core, não para uma cópia do viewer.

### Na revisão (Claude, PR #33)

- **O destaque durava um único quadro.** A primeira versão comparava a tela com a do quadro de
  interface anterior: um byte que muda *uma vez* (uma flag de história, justamente o que você vai
  procurar) acendia por 1/60 s, e num monitor de 144 Hz quase nunca, porque na maioria dos quadros
  de interface o emulador nem rodou. Agora o `ByteChangeTracker` (na `cw_devtools`, testado) guarda
  **em que frame do emulador** cada byte mudou e mantém o destaque por 60 frames (~1 s),
  esmaecendo. Como o tempo é o do emulador:
  - pausado (P), o destaque **congela**: dá para olhar com calma;
  - avançando um frame (N), ficam acesos **exatamente os bytes que mudaram naquele frame**, a
    técnica clássica para achar o endereço de uma ação ("apertei ✕ neste frame: o que mudou?").
- Lição: os testes (corretos) checavam a função de comparação; o problema era de *uso*, e só
  aparece pensando em como a ferramenta vai ser usada na pesquisa de memória.

### Para fixar

1. Por que `MemoryWriter` é uma interface separada de `MemoryReader`?
2. Quais duas fronteiras a escrita libretro valida antes do `memcpy`?
3. Com os bytes `12 34 56`, qual valor de 24 bits aparece em little-endian e em big-endian?
4. Se o byte é `0xA5`, que byte resulta ao trocar o nibble baixo para `2`? E o alto para `3`?
5. O que deixa de ser comparável quando um endereço não estava na lista visível do snapshot anterior?
6. Por que `ImGuiListClipper` importa para uma região de 2 MiB, mesmo que a grade mostre só 16 bytes por linha?

---

## 2.3 · Busca de memória

### O que foi feito

- `MemorySearch` (`src/devtools/include/cw/devtools/memory_search.hpp` e
  `src/devtools/memory_search.cpp`) implementa snapshots e filtros sem SDL, ImGui ou libretro. A
  busca inicial percorre o intervalo escolhido e só guarda candidatos cujos bytes estão legíveis e
  cabem inteiros no intervalo.
- Cada candidato é um endereço interpretado com um tamanho e byte order selecionados. O modo
  **All bits (0–7)** cria um candidato por par endereço/bit, ou 16.777.216 candidatos para 2 MiB.
  A leitura usa `decode_memory_value`, a mesma função do viewer, então bit 0–7, nibble baixo/alto e
  valores de 8/16/24/32 bits têm a mesma interpretação nas duas janelas. A opção de alinhamento
  limita endereços de 16 bits a múltiplos de 2 e endereços de 32 bits a múltiplos de 4.
- O filtro compara o valor vivo com o snapshot anterior, o primeiro snapshot ou um número digitado,
  usando `=`, `!=`, `<`, `>`, `<=` ou `>=`. Os candidatos que continuam passam a guardar também a
  foto atual como referência anterior. Os quatro filtros mais recentes podem ser desfeitos.
- O bitmap de candidatos usa um bit por candidato; os snapshots guardam cada valor com o menor
  número de bits possível. Isso representa também os 16 milhões de candidatos de **All bits** sem
  um objeto por linha. Um índice de posições permite pedir as linhas visíveis diretamente. A
  memória extra do desfazer fica limitada a quatro estados.
- **View → Memory Search** permite escolher `All` ou uma região, tamanho, byte order e alinhamento,
  iniciar uma busca e filtrar. A tabela virtualizada mostra endereço, valor atual, snapshot anterior
  e primeira foto. Clicar num endereço abre o viewer no mesmo endereço e região.
- A janela lê e filtra no loop principal, depois de `retro_run()` e antes de desenhar. Ela continua
  disponível com o jogo pausado (P); o avanço de um frame (N) atualiza a memória antes do próximo
  filtro.

### Método de busca: um exemplo

Para localizar uma variável que identifica a sala atual:

1. Pause na sala A com **P** e clique em **New Search** para guardar a primeira foto.
2. Vá à sala B e filtre com `!=` / **Previous snapshot**. Restam os endereços que mudaram.
3. Ainda na sala B, pause ou avance quadros até a memória estabilizar. Filtre com `=` /
   **Previous snapshot**; os valores que continuam mudando saem da lista.
4. Volte à sala A e filtre com `=` / **First snapshot**. O teste sintético reproduz essa sequência
   e termina com um único endereço.
5. Clique nesse endereço para inspecioná-lo no viewer. Repita com outras transições se ainda houver
   mais de um candidato.

O valor **atual** da tabela é lido ao desenhar cada linha. **Previous snapshot** e **First snapshot**
são valores guardados pela última busca/filtro e pela busca inicial, respectivamente.

### Conceitos

**Um candidato tem o tamanho escolhido.** Para uma busca de 8 bits, cada endereço do intervalo pode
ser candidato. Para 16/24/32 bits, cada endereço inicial também é considerado, exceto se o valor
ultrapassar o fim do intervalo, a leitura encontrar bytes indisponíveis ou o alinhamento opcional o
excluir. Endereços de vários bytes podem se sobrepor, como no viewer. Em **All bits**, cada byte
gera oito candidatos separados, do Bit 0 ao Bit 7; a tabela os identifica com um sufixo como
`0x000123.Bit5`.

**As três referências respondem perguntas diferentes.** Comparar com o snapshot anterior encontra
o que mudou desde o último filtro. Comparar com o primeiro snapshot encontra valores que voltaram
ao estado inicial. Comparar com um valor digitado busca um número conhecido. O filtro atualiza a
referência anterior dos sobreviventes; o primeiro snapshot nunca muda.

**Bitmap, valores compactados e índice de linhas.** O bitmap ocupa um bit por endereço candidato.
Valores de bit são compactados em um bit, nibbles em quatro, e tamanhos maiores em 8/16/24/32 bits.
O índice de postos permite que a tabela encontre, por exemplo, o candidato de número 1.000.000 sem
recontar todos os anteriores. Assim a busca pode reter milhões de endereços sem criar um objeto C++
por linha.

**Desfazer preserva a busca como estava.** Cada filtro guarda o bitmap e o snapshot anterior antes
de alterar os resultados. A fila mantém no máximo quatro desses estados, para que o consumo de
memória não cresça a cada refinamento.

**A interpretação não pode ficar obsoleta.** Mudar tamanho, byte order, alinhamento ou intervalo
descarta os resultados. Trocar de sessão também invalida a busca, mesmo se o mesmo jogo for
carregado novamente com o mesmo mapa de memória.

### Arquivos para ler

1. `src/devtools/include/cw/devtools/memory_search.hpp` e `src/devtools/memory_search.cpp`: estados,
   bitmap, snapshots compactos, filtros e desfazer.
2. `tests/test_memory_search.cpp`: operadores, tamanhos, byte orders, intervalos, alinhamento,
   referências, sequência das salas e invalidação.
3. `tests/test_libretro_memory.cpp`: uma busca cobrindo o mapa PS1 parcial do core sintético.
4. `src/frontend/main.cpp`: janela **Memory Search**, clipper da tabela e navegação para o viewer.
5. `src/devtools/include/cw/devtools/memory.hpp` e `src/devtools/memory.cpp`:
   `decode_memory_value`, usado também pelo viewer.
6. `reference/RAIntegration/src/services/Search/SearchImpl.cpp`,
   `SearchImpl_4bit.hh`, `SearchImpl_16bit_be.hh`, `SearchImpl_24bit.hh` e
   `reference/RAIntegration/src/ui/viewmodels/MemorySearchViewModel.cpp` (referência somente de
   leitura; nenhum código foi copiado).

### O que aprendemos com a referência

- A RAIntegration aplica filtros sobre blocos de memória capturados e guarda resultados de busca
  separados; a implementação daqui é menor, mas segue as mesmas interpretações de valor e
  comparação. A referência também confirma que os nibbles baixo e alto podem ser resultados
  distintos e que o 16-bit big-endian lê os bytes na ordem numérica correta.
- O 24-bit lê exatamente os três bytes do tamanho selecionado. Usar `decode_memory_value` evita
  que a busca trate valores de 24 bits ou byte order de forma diferente do viewer.

### Verificação

- `cmake -S . -B build`, `cmake --build build -j` e `ctest --test-dir build --output-on-failure`:
  passaram sem warnings; 78 casos e 6.147 asserções.
- Build Release e `ctest` Release: passaram. Build com ASan+UBSan e `ctest` sanitizado: passaram.
- Em Release, mediana de três execuções: filtro de 8 bits sobre 2.097.152 candidatos em **85,1 ms**;
  um filtro **All bits** sobre 16.777.216 candidatos em **677,8 ms**. O tempo cobre o filtro, não
  a criação inicial do snapshot.
- A lógica rodou com o core sintético e o mapa `ps1_partial`; foram encontrados exatamente os
  1.048.576 bytes legíveis, sem candidatos na parte indisponível.
- O smoke test offscreen carregou o Beetle PSX e o `.cue`, montou o mapa de memória PS1 e rodou
  583 frames até o timeout. O log do core avisou que faltava `scph5500.bin`; não encontrei um BIOS
  PS1 nos diretórios padrão verificados (`~/.local/share/cheevo-workbench/system`,
  `~/.config/retroarch/system`, `~/.local/share/retroarch/system` ou `/usr/share/libretro/system`).
  Portanto, o carregamento do core/conteúdo foi verificado, mas ainda falta confirmar o jogo em
  execução e a interação visual com a busca; o roteiro manual está no rascunho do PR.

### Problemas encontrados

- Um teste de alinhamento 32-bit esperava um endereço cujo valor terminava depois do fim do
  intervalo. Corrigi a fixture para separar claramente alinhamento de limite; a implementação
  excluiu corretamente aquele endereço.
- O rascunho indicado em `/tmp/cheevo-pr-issue-11.md` não estava presente no ambiente; recriei-o
  com a descrição, decisões, verificações e o roteiro manual.

### Para fixar

1. Qual pergunta responde `Previous snapshot` que `First snapshot` não responde?
2. Por que um valor de 32 bits no último endereço do intervalo não pode ser candidato?
3. Como o bitmap e o índice permitem representar e listar milhões de candidatos sem um objeto por
   endereço?
4. O que o desfazer restaura além da lista de endereços?
5. Por que trocar só o byte order exige uma nova busca, mesmo que a faixa continue igual?
6. Rode a sequência da sala A/B com o jogo pausado e avanço de quadro. Em que momento `Previous`
   é atualizado e em que momento `First` permanece igual?

---

## 2.4 · Watch list

### O que foi feito

- `WatchList` (`src/devtools/include/cw/devtools/watch_list.hpp` e
  `src/devtools/watch_list.cpp`) guarda endereço, tamanho, byte order e rótulo opcional. A chave de
  duplicata é endereço + tamanho + ordem; a lista também permite remover e reordenar itens.
- A cada atualização, `WatchList::update` usa `decode_memory_value`, a mesma interpretação do
  viewer e da busca. Cada item guarda o valor atual, o anterior e o frame emulado da última mudança.
  Se a leitura falha, o atual fica indisponível; um valor antigo nunca é apresentado como atual.
- **View → Watch List** mostra endereço clicável, interpretação, valor hexadecimal e decimal,
  anterior, rótulo, destaque temporário e ações para subir, descer e remover. A janela permite
  adicionar um endereço diretamente; o viewer e cada resultado da busca também têm um botão
  **Watch**. Resultados de busca por bit preservam o bit selecionado.
- O loop atualiza a lista uma vez depois dos frames rodados, na thread principal. O destaque dura
  até 60 frames emulados: pausado, não envelhece; ao descarregar o jogo, os itens permanecem e os
  valores passam a indisponíveis. A lista não é gravada em arquivo; persistência fica para os
  bookmarks do passo 5.2.
- Os testes usam `BufferMemory` para tamanhos, ordens, duplicatas, reordenação, remoção, falhas de
  leitura e retorno da memória. O core sintético também expõe um contador de 32 bits no mapa PS1;
  o teste acompanha os frames e confirma que o item fica indisponível após `core.unload()`.

### Conceitos

**Um watch é uma interpretação, não só um endereço.** O mesmo endereço pode ser observado como
8 bits, 16 bits little-endian ou 16 bits big-endian. Por isso a duplicata só é recusada quando
endereço, tamanho e byte order coincidem. Bits e nibbles também usam o decoder compartilhado.

**Atual indisponível não é o mesmo que zero.** `current_value` é opcional. Um endereço sem memória,
fora do buffer ou numa região não legível mostra “Unavailable”; não reutiliza o último valor como
se a leitura tivesse dado certo. O anterior conserva a última observação válida para comparar
quando a memória volta.

**O relógio do destaque é o do jogo.** A lista compara o estado entre atualizações, mas registra o
frame de emulação em que encontrou a mudança. Chamadas repetidas enquanto pausado continuam no
mesmo frame, então o destaque congela. Ao trocar de sessão ou quando o contador volta a zero após
unload/reload, os timestamps antigos são descartados sem remover os watches nem os valores lidos.

### Arquivos para ler

1. `src/devtools/include/cw/devtools/watch_list.hpp` e `src/devtools/watch_list.cpp`: modelo puro,
   atualização e destaque por frame.
2. `tests/test_watch_list.cpp`: tamanhos/endians, sequência de frames, pausa, indisponibilidade,
   recuperação, duplicatas, ordenação e remoção.
3. `tests/test_libretro_memory.cpp` e `tests/test_core/test_core.c`: contador de frames na memória
   mapeada como PS1 e leitura segura depois do unload.
4. `src/frontend/main.cpp`: menu, janela, ligação com viewer/busca e atualização após `retro_run`.
5. `reference/RAIntegration/src/ui/viewmodels/MemoryWatchListViewModel.cpp` e
   `MemoryWatchViewModel.cpp` (somente leitura; nenhum código foi copiado).

### O que aprendemos com a referência

- A RAIntegration atualiza seus view models durante o frame e também reage a escritas em memória.
  Aqui, a lista é um modelo pequeno e independente da UI: lê os itens uma vez por passagem do loop,
  depois dos frames do core, usando `MemoryReader` e `decode_memory_value`.
- Este passo não lê nem escreve bookmarks da RAIntegration. A lista fica apenas na sessão do
  programa; o formato compatível será tratado no passo 5.2.

### Verificação

- `cmake -S . -B build`, `cmake --build build -j` e `ctest --test-dir build --output-on-failure`:
  passaram sem warnings; 97 casos e 6.312 asserções.
- Build separado com ASan+UBSan e
  `ctest --test-dir /tmp/cheevo-issue12-asan --output-on-failure`: passaram.
- Smoke test offscreen com Beetle PSX, BIOS e o `.cue` das notas: o core encontrou a BIOS, montou
  o mapa PS1 de 2.098.176 bytes, carregou o jogo e rodou 229 frames antes do encerramento
  controlado por timeout. O backend offscreen não permite conferir visualmente cliques, entrada de
  texto ou cores do destaque; essa parte fica para o roteiro manual.

### Problemas encontrados

- A primeira compilação apontou inicialização parcial dos campos opcionais do item. Eles foram
  inicializados explicitamente; a compilação seguinte passou sem warnings.

### Para fixar

1. Por que endereço + tamanho + byte order identificam um item, em vez de apenas o endereço?
2. Quando a memória desaparece, por que o valor atual fica vazio enquanto o anterior é mantido?
3. O que acontece com o destaque se a lista é atualizada vinte vezes sem o jogo avançar?
4. Como o teste confirma que `core.unload()` não deixa a lista lendo um ponteiro antigo?
5. Por que os watches sobrevivem ao unload do jogo, mas seus valores não?
