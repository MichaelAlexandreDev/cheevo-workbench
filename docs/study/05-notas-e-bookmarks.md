# Etapa 5 — Notas no editor e bookmarks

## 5.2 — Bookmarks compatíveis com o RAIntegration

**O que foi feito**

A Watch List agora é carregada por game ID e fica salva em
`$XDG_DATA_HOME/cheevo-workbench/bookmarks/<GameID>-Bookmarks.json`. O JSON segue o formato de
bookmarks do RAIntegration. Adicionar, renomear, reordenar, remover ou mudar entre Decimal e Hex
salva automaticamente no arquivo local, com gravação atômica. A integração continua exibindo
valores ao vivo, mudanças recentes, endereços indisponíveis e code notes.

Os formatos antigos aceitos pelo RAIntegration também são lidos. Entradas com tamanho que a
Watch List não representa e endereços indiretos são preservados no arquivo e identificados como
`unsupported`, sem leitura de valor. Importar de e exportar para `RACache/Bookmarks/` são ações
explícitas; a exportação pede confirmação ao substituir um arquivo. Um JSON local malformado é
mostrado como erro e bloqueia gravações para proteger o original.

**Conceitos**

- O game ID seleciona a lista e o caminho local; trocar de jogo carrega outra lista sem misturar
  os bookmarks.
- RAIntegration grava uma lista na propriedade JSON `Bookmarks`. O formato atual salva endereço
  e tamanho juntos em `MemAddr` (por exemplo, `0xH0d9c24`); `Decimal` só aparece quando ligado e
  `Description` só quando há um nome.
- Os formatos antigos usam `Type` com `Address`, ou `Size` com `Address`. `MemAddr` pode conter
  endereços indiretos que dependem de ponteiros e não podem ser lidos pela Watch List atual.
- Cada mutação escreve primeiro em um arquivo temporário único e o renomeia sobre o arquivo
  local. O RACache é separado e só muda quando o usuário escolhe Export.
- Uma entrada desconhecida mantém seu objeto JSON original. Isso conserva propriedades futuras
  do RAIntegration ao salvar mudanças feitas em outros bookmarks.

**Arquivos para ler**

1. `src/devtools/bookmarks.cpp` — formatos JSON, caminho, preservação e gravação atômica.
2. `src/devtools/include/cw/devtools/bookmarks.hpp` — modelo e operações do serviço.
3. `src/frontend/main.cpp` — troca por game ID, controles da Watch List e importação/exportação.
4. `tests/test_bookmarks.cpp` — dados sintéticos e casos de compatibilidade.

**O que aprendemos com a referência**

`reference/RAIntegration/src/ui/viewmodels/MemoryBookmarksViewModel.cpp` aceita três formas: o
formato atual com `MemAddr`, um formato antigo com `Type` e `Address`, e outro com `Size` e
`Address`. O writer atual usa `MemAddr`, e adiciona `Decimal` e `Description` apenas quando
necessário. O teste do RAIntegration confirma detalhes como o prefixo de tamanho e endereço em
hexadecimal minúsculo. `AchievementLogicSerializer.cpp` escreve tamanhos no prefixo de `MemAddr`;
`FileLocalStorage.cpp` confirma `RACache/Bookmarks/<GameID>-Bookmarks.json`.

O parser usa `rc_parse_memref` do rcheevos fixado em `third_party/rcheevos`, o mesmo parser de
endereços usado pelo RAIntegration através do rcheevos. O formato local é compatível com o cache,
mas mantém os dados do usuário em diretório separado para que a gravação automática não altere o
RACache.

**Problemas encontrados**

- As entradas `Text` e `BitCount` existem no formato antigo, mas não têm uma interpretação
  correspondente na Watch List. Mantê-las como JSON preservado evita perder dados ao editar outra
  linha.
- Um arquivo malformado não pode ser tratado como lista vazia: a próxima edição apagaria os dados
  existentes. A leitura inválida, por isso, desativa gravações até o usuário corrigir o arquivo.
- A forma e a largura dos dígitos de `MemAddr` são parte do formato. Os testes usam um endereço
  compatível com uma memória de PS1 para verificar seis dígitos e prefixo em minúsculo.

**Na revisão**

A preservação inicial cobria somente entradas não suportadas. Editar uma entrada suportada
apagava propriedades desconhecidas dessa entrada e do objeto principal do JSON. Dois testes de
regressão reproduziram a perda antes da correção. Agora normalizamos os campos conhecidos para
`MemAddr`, conservando as extensões tanto nas entradas quanto no documento. Os metadados de uma
importação só substituem os atuais depois de a gravação local terminar com sucesso.

Também verificamos falhas de gravação: se o destino não puder ser substituído, a lista em memória
e seus metadados continuam intactos, e o arquivo temporário é removido. Os testes cobrem os 17
formatos inteiros de `MemAddr` suportados pela Watch List, inclusive os big-endian, e a preservação
de entradas de texto e ponto flutuante como `unsupported`.

O build inicial dos testes encontrou o JSON instalado no sistema, porque faltava declarar a
dependência de `nlohmann_json` no alvo `cw_tests`. Conferir os headers realmente usados pelo
compilador revelou o problema. O vínculo explícito no CMake agora seleciona o submódulo
vendorizado, mesmo em máquinas que também têm uma instalação global da biblioteca.

A validação da revisão terminou com 128 casos e 6.600 verificações passando, build sem warnings
e testes com ASan+UBSan. Duas mutações deliberadas foram detectadas: retirar a proteção contra
gravar sobre JSON inválido e trocar a leitura big-endian por little-endian. O código foi
restaurado depois. LeakSanitizer ficou desativado pela limitação de ptrace do sandbox.

O core PS1 executou 600 frames de Twilight Syndrome em um programa auxiliar, verificando
persistência, ordem, formato, leituras ao vivo, exportação e troca de game ID com dados de teste
em `/tmp`. A aplicação gráfica também iniciou e confirmou o hash e o ID `18238`; os controles
interativos da Watch List ainda dependem do teste manual do usuário.

Uma revisão posterior reproduziu duas falhas na interface: fechar o aplicativo com o campo de
nome ainda ativo perdia a renomeação, e a coluna de ações cortava quase todo o botão `Remove`.
Agora o encerramento salva os nomes pendentes, inclusive com a Watch List oculta ou o aplicativo
minimizado. Se a gravação falhar, o aplicativo permanece aberto, mostra o erro e conserva o
rascunho para uma nova tentativa. A edição continua sendo salva normalmente ao sair do campo.
A largura da coluna `Actions` é calculada pelo texto dos três botões, pelo espaçamento e pela
fonte atual; ela não pode ser reduzida manualmente ou por uma largura antiga salva no layout.

Um programa temporário com ImGui, sem janela gráfica e com dados sintéticos, confirmou as duas
regressões antes da correção. Depois, suas oito verificações passaram, cobrindo também falha de
gravação com a Watch List oculta, preservação do rascunho e nova tentativa de encerramento.

**No teste do usuário**

Up/Down e persistência dos nomes funcionaram, mas o seletor de tamanho ainda deixava o rótulo
`Size` fora de um painel estreito. Ajustar apenas a coluna de ações não resolvia os formulários
acima da tabela. Os campos agora usam a largura disponível e os rótulos ficam acima deles;
tamanho e byte order compartilham uma linha somente quando cabem. Importar/exportar também
mudam de linha, e a tabela tem rolagem horizontal dentro do painel quando necessário. O editor
de valor do Memory Viewer recebeu o mesmo tratamento. Um probe ImGui verificou nove combinações
de largura (320/480/900 px) e escala de fonte (1/1,5/2), sem exigir que a janela fosse ampliada.

O usuário confirmou o funcionamento do layout corrigido na interface real. Up/Down,
persistência dos nomes e os ajustes responsivos estão validados manualmente.

**Para fixar**

1. Por que a Watch List guarda seu arquivo automático fora de `RACache`?
2. O que acontece ao carregar `Size: 14`, e por que esse bookmark não deve ser descartado?
3. Qual é a diferença entre ler `Type: 2` e `Size: 2` nos formatos antigos?
4. Como a gravação temporária seguida de rename evita deixar metade do JSON em caso de falha?
