# 02 · Etapa 2: memória

Objetivo da etapa: pesquisar a memória do Twilight Syndrome dentro da ferramenta (viewer, busca,
watch list, code notes). É aqui que o trabalho no set começa.

| Subetapa | Conteúdo | Status |
|---|---|---|
| 2.1 | Memória em endereços do RetroAchievements | ✅ |
| 2.2 | Memory viewer e edição de valores | ⏳ |
| 2.3 | Busca de memória | ⏳ |
| 2.4 | Watch list | ⏳ |
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

**Cópia dos descritores.** O array que o core passa no `SET_MEMORY_MAPS` pode não existir mais
depois da chamada; copiamos os descritores (os ponteiros *dentro* deles continuam válidos enquanto
o jogo estiver carregado).

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
6. Exercício: rode o programa com o jogo e veja, na janela Status, as 3 regiões. Depois edite
   `kKnownCores` em `libretro_memory.cpp` removendo o Beetle PSX: o que muda na janela, e por quê?
