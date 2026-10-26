# 00 · Planejamento

## O problema

No Linux já dá para **jogar** com conquistas (RetroArch, DuckStation, PCSX2), mas as ferramentas
para **criar** conquistas só existem para Windows:

| Ferramenta | O que é | Papel |
|---|---|---|
| **RALibretro** | Emulador (front-end libretro) | Carrega o core e o jogo; vídeo, áudio, input, save states |
| **RAIntegration** | DLL de ferramentas de dev | Memory inspector, editor de conquistas, code notes, Rich Presence, upload |

O RAIntegration é um **plugin**: o emulador entrega a memória do jogo a cada frame e a DLL desenha
as janelas de dev. É separado porque a RA usa a mesma DLL em vários emuladores.

## Por que aqui é uma ferramenta só

- Só temos um emulador (libretro já cobre todos os consoles via cores).
- A DLL é Win32 e não roda no Linux; o que importa ser compatível são os **arquivos**
  (`RACache/Data/<GameID>-User.txt`, `-Rich.txt`, bookmarks).
- Mas por dentro mantemos a divisão:
  - `src/frontend/` → papel do RALibretro
  - `src/devtools/` → papel do RAIntegration (biblioteca que só enxerga "um bloco de memória",
    testável sem jogo nenhum)

## Opções avaliadas

| Opção | Ideia | Decisão |
|---|---|---|
| A | Projeto novo do zero, originais só como referência | ✅ **escolhida** (aprendizado) |
| B | Portar o RAIntegration (trocar Win32 por ImGui) | ❌ código grande, só MSVC |
| C | Novo, reaproveitando o `devkit` do RAIntegration | ❌ |

Os originais ficam em `reference/` (fora do git) para **ler**, nunca copiar. O RALibretro é GPL-3:
copiar código dele obrigaria o projeto a ser GPL-3.

**Atenção:** `reference/rcheevos` é o `master`, que tem pastas diferentes da versão que compilamos.
Para estudar o rcheevos use `third_party/rcheevos` (v12.5.0).

## Licenças das dependências

| Projeto | Licença | Como usamos |
|---|---|---|
| rcheevos | MIT | Biblioteca (submódulo) |
| Dear ImGui | MIT | Biblioteca (submódulo) |
| doctest | MIT | Testes (submódulo) |
| libretro-common | MIT | Só o `libretro.h` |
| SDL3 | zlib | Do sistema (`pacman`) |
| RAIntegration | MIT | Só leitura (`reference/`) |
| RALibretro | GPL-3.0 | Só leitura (`reference/`) |

## Etapa 7 (servidor) e por que está bloqueada

O rcheevos já monta todas as requisições ao servidor (`rc_api_init_login_request`,
`rc_api_init_update_achievement_request`, …). A barreira é **permissão**, não técnica: escrever no
site oficial pode corromper sets e usa a sua conta. Até a equipe do RA aprovar, o fluxo é:
desenvolver aqui → abrir o RAIntegration oficial (Wine) → ele lê os mesmos arquivos → upload.

## Code notes: o `-Notes.json` é cache, não arquivo local

Descoberto na revisão do Codex (e conferido): o RAIntegration grava em
`RACache/Data/<GameID>-Notes.json` a **resposta do servidor** com as notas
(`reference/RAIntegration/src/devkit/context/impl/RcClient.cpp`, `CacheCodeNotesResponse`) e só lê
esse arquivo no modo offline.

As notas que você cria ou altera no RAIntegration **não** vão direto para o servidor: ficam no
`-User.txt` como linhas `N0:0x001234:"texto"` (`MemoryNotesModel.cpp`, `Serialize`) e são enviadas
junto com as conquistas, no upload (`AssetUploadViewModel.cpp`, `QueueMemoryNotes`).

Decisão (2026-10-26): mesmo assim, as **suas** notas ficam num arquivo nosso
(`~/.local/share/cheevo-workbench/notes/`); as do servidor aparecem só para leitura, a partir do
cache; e uma lista "para publicar" mostra o que copiar no RAIntegration. As notas vieram para o
passo **2.5**, junto da pesquisa de memória, que é quando você mais anota. Exportar as notas
como linhas `N0:` no `-User.txt` (para o RAIntegration enviá-las sozinho) ficou como opção futura.

Lição: o nome de um arquivo não diz para que ele serve; é preciso ver **quem escreve** e **quem lê**.
E uma afirmação só vale depois de conferida no código: aqui, duas suposições minhas estavam erradas.

## Para fixar

1. Por que o RAIntegration é uma DLL e não um programa?
2. O que precisa ser compatível com o RAIntegration: o código ou os arquivos?
3. Por que `devtools/` não pode depender do SDL?
4. Por que não podemos copiar código do RALibretro, mas poderíamos copiar do RAIntegration?
5. Por que não gravamos as suas code notes no `-Notes.json`?
