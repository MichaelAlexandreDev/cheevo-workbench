# 04 · Etapa 4: editor de conquistas e arquivos locais

Objetivo da etapa: editar conquistas no Linux e produzir arquivos locais compatíveis com o
RAIntegration, para publicação posterior pela ferramenta oficial.

| Subetapa | Conteúdo | Status |
|---|---|---|
| 4.1 | Modelo editável de lógica e serializador | ✅ |
| 4.2 | Leitura e escrita de `-User.txt` | ✅ |
| 4.3 | Interface do editor | ⏳ |
| 4.4 | Estado de cada condição ao vivo | ⏳ |

## 4.1 · Modelo editável de lógica

### O que foi feito

`AchievementLogic` representa um grupo Core e uma lista ordenada de grupos Alt. Cada grupo
contém condições com flag, operando esquerdo, operador, operando direito opcional e alvo de hits.
Os operandos representam endereço, delta, prior, BCD, inversão, inteiro, float e Recall. Funções
legadas `@name` são preservadas, embora o rcheevos não implemente sua avaliação.

`parse_achievement_logic()` transforma a string em valores editáveis. A leitura usa o parser do
rcheevos v12.5.0 para reconhecer condições e operandos, mantendo as condições na ordem textual.
`serialize_achievement_logic()` monta a string e verifica o resultado com o mesmo parser.
Tudo fica em `cw_devtools`, sem SDL, interface gráfica, arquivos ou acesso a servidores.

Essa subetapa é a base interna do editor. A janela de edição pertence à 4.3; a leitura e escrita
de `RACache/Data/<GameID>-User.txt` pertencem à 4.2.

### Conceitos

**Modelo de edição e modelo de execução têm objetivos diferentes.** O runtime do rcheevos
reorganiza condições para avaliar pausas, resets e outras classes eficientemente, além de
transformar cadeias de operações em referências modificadas. O editor precisa preservar a ordem
que o autor escreveu. Por isso, lemos com `rc_parse_condition_internal()` e `rc_parse_operand()`
em um contexto temporário, antes dessas transformações, e copiamos apenas os valores necessários.
A estrutura pública não guarda ponteiros nem contadores do runtime.

**Preservar texto exige guardar mais do que o valor.** `hA`, `10` e `00010` podem representar o
mesmo inteiro. `=` e `==` representam a mesma comparação, e os hits aceitam `.3.` e `(3)`.
Cada token guarda sua grafia e uma referência ao valor original. Ao serializar, um token que
continua com o mesmo valor reutiliza sua grafia. Um token alterado recebe a forma canônica.
Desfazer uma edição também recupera a grafia inicial.

O alvo de hits é um `std::optional<uint32_t>`: ausência, `.0.` e `.3.` não são a mesma escrita.
`required_hits.reset()` remove o sufixo; atribuir zero mantém um alvo explícito com valor zero.
Isso permite preservar uma escrita legada e ainda oferecer uma remoção explícita no editor.

Por exemplo, ao ler `p:0Xh00aF==hA(03)` e mudar apenas o valor da direita para 11, o resultado
é `p:0Xh00aF==11(03)`: a flag minúscula, o endereço e o alvo continuam intactos. Novos endereços
usam prefixos de tamanho compatíveis com o RAIntegration e pelo menos seis dígitos hexadecimais,
suficientes para a RAM do PS1; endereços maiores não são truncados. A largura visual exata usada
pelo RAIntegration depende do contexto do console, portanto não é universal.

**Separadores dependem do contexto.** O `S` de `0xS0010` seleciona o bit 6; outro `S`, após uma
condição, inicia um grupo Alt. O espaço em `0x 0010` seleciona 16 bits e não pode ser removido
como se fosse espaçamento decorativo. O parser também aceita grupos vazios e `s` minúsculo entre
grupos, com as restrições da versão compilada.

**Validar o prefixo não basta.** `rc_trigger_size()` pode aceitar um prefixo válido seguido por
texto extra. A leitura local exige consumir toda a entrada e rejeita NUL embutido. Depois disso,
a validação do trigger completo detecta restrições entre condições, como alvos Measured
incompatíveis. Os erros de leitura indicam grupo, condição e posição do token quando disponíveis.

**Uma edição não pode mudar silenciosamente de valor.** Antes de devolver a string, o serializer
a lê novamente e compara os campos e a estrutura com o modelo solicitado. Isso detecta, por
exemplo, floats que o parser truncaria ou nomes de função que engoliriam um separador.
Floats usam ponto decimal e não usam notação exponencial; valores não finitos são rejeitados.
A entrada e a saída têm limite de 1 MiB.

### Arquivos para ler (nesta ordem)

1. `src/devtools/include/cw/devtools/achievement_logic.hpp`: grupos, condições e operandos.
2. `src/devtools/achievement_logic.cpp`: leitura, preservação dos tokens e serialização validada.
3. `tests/test_achievement_logic.cpp`: exemplos sintéticos e edições estruturais.
4. `third_party/rcheevos/src/rcheevos/condition.c`: flags, operadores e hits.
5. `third_party/rcheevos/src/rcheevos/operand.c` e `memref.c`: operandos e tamanhos.
6. `third_party/rcheevos/src/rcheevos/condset.c` e `trigger.c`: grupos e validação completa.
7. `reference/RAIntegration/src/devkit/data/util/AchievementLogicSerializer.cpp`: grafia emitida
   pela ferramenta de referência, usada para conferir a compatibilidade.

### O que aprendemos com a referência

- O serializer do RAIntegration emite flags maiúsculas, `=` para igualdade e `.N.` para hits;
  zero não precisa de sufixo. Esses formatos orientam a escrita de campos novos ou alterados.
- Os tamanhos de lógica incluem bit count, 16/24/32 bits big-endian e seis formatos de memória
  em ponto flutuante (`fF`, `fB`, `fH`, `fI`, `fM`, `fL`). O enum do viewer não cobre tudo isso,
  então o modelo de lógica tem seu próprio enum de tamanhos.
- Em `A:`, `B:` e `I:`, o rcheevos aceita comparações legadas e ignora a parte comparada na
  execução. O modelo expõe o efeito real (`op=None`, sem direita nem alvo de hits) e mantém
  a escrita legada em metadados enquanto a condição permanecer intacta. Após uma edição,
  emite a forma efetiva sem o comparador ignorado. Tentar criar uma nova comparação desse
  tipo produz erro, em vez de sugerir que a comparação terá efeito.
- `M:` sem operador é permitido em expressões de valor, mas não em triggers de conquistas.
  A validação precisa usar o contexto certo, não apenas o parser isolado de uma condição.

### Problemas encontrados

- **Uma leitura aceita não garante uma escrita fiel:** reconstruir tudo no formato canônico
  apagaria diferenças de caixa, zeros à esquerda e formas legadas. Os metadados privados de
  preservação evitam isso sem exigir que cada consumidor gerencie flags de alteração.
- **Testar apenas ida e volta deixaria uma lacuna:** um token intacto reutiliza seu texto e não
  exercita a tabela de escrita canônica. Os testes também alteram endereços, tipos, flags e
  alvos, relêem a saída e verificam os campos, além de conferir a aceitação pelo rcheevos.
- **O parser isolado usa APIs internas:** essa dependência fica restrita ao `.cpp` e à versão
  fixada do submódulo. Uma atualização do rcheevos deve passar pelos testes de compatibilidade
  antes de mudar a versão compilada.
- **Na revisão:** comparações ignoradas em `A/B/I` passaram a aparecer no modelo pelo efeito
  real, e o alvo de hits passou a ser opcional para permitir remover um zero explícito. Os
  testes verificam preservação, edição e desfazer nesses dois casos.

### Verificação

Build e formatação passaram sem avisos. A suíte passou com **161 casos e 8.594 asserções**,
incluindo execução com AddressSanitizer e UndefinedBehaviorSanitizer; LeakSanitizer foi
desativado por limitação do ambiente. Os exemplos são sintéticos e as saídas são conferidas com
`rc_trigger_size()` e `rc_parse_trigger()` da biblioteca compilada. A comparação de eventos em
memória sintética também confirma a equivalência de uma lógica após edição.

Um teste de mutação trocou temporariamente o prefixo canônico de 8 bits pelo de 24 bits. O teste
de compatibilidade falhou; após restaurar o código, a suíte completa voltou a passar. Isso
confirma que a bateria detecta uma escrita incorreta, além de preservar strings intactas.

Esta subetapa não altera a interface do aplicativo. A verificação direta pode ser repetida com
`ctest --test-dir build --output-on-failure`; a edição visual será testada na 4.3.

### Para fixar

1. Por que serializar diretamente as condições do trigger de runtime pode mudar a ordem escrita?
2. O que se perde ao guardar somente o número 10, sem a grafia do token que o representava?
3. Por que `0xS0010` não pode ser dividido procurando todos os caracteres `S` da string?
4. Qual é a diferença entre um grupo Core vazio e a ausência de grupos Alt?
5. Por que o serializer lê novamente a string depois de montá-la?
6. Exercício: nos testes, crie uma condição em um grupo Alt, altere seu alvo de hits, mova-a para
   o Core e verifique a string e o modelo relido. Use apenas endereços e dados sintéticos.

## 4.2 · Arquivos locais `-User.txt`

### O que foi feito

`UserFileDocument` representa os cabeçalhos e os registros de um arquivo local compatível com
`RACache/Data/<GameID>-User.txt`. Conquistas usam o modelo de lógica da 4.1. Leaderboards guardam
Start, Cancel e Submit nesse modelo e a expressão Value como texto, pois ela usa outra gramática.
Essa etapa não ativa nem submete leaderboards; a avaliação ao vivo pertence à 6.2.

`parse_user_file()` e `serialize_user_file()` trabalham somente com texto. `UserFileStore` é a
camada de acesso explícito ao disco: carrega, verifica alterações externas e salva por substituição
atômica. Não há integração automática com o RACache nem controles novos na interface. Eles virão
no editor da 4.3.

### Conceitos

**O cabeçalho não é uma versão de esquema.** O RAIntegration lê e descarta as duas primeiras
linhas: a versão do programa que escreveu e o título do jogo/subset principal. Não compara a
primeira linha com uma lista de versões aceitas. Portanto, guardamos esse texto literalmente,
inclusive versões desconhecidas. Arquivos novos identificam o programa como
`cheevo-workbench 0.1.0`.

**O registro tem uma posição para cada campo.** Os campos são separados por `:`, mas texto entre
aspas pode conter esse caractere sem encerrar o campo. O ID pode ter um subset explícito, como
`17|42`. Um ID zero solicita que o RAIntegration atribua um ID local ao importar; não é o game ID.
O game ID faz parte do nome do arquivo.

| Campo | Conquista | Leaderboard |
|---|---|---|
| 1 | ID, com subset opcional | `L` + ID, com subset opcional |
| 2 | Trigger | Start |
| 3 | Título | Cancel |
| 4 | Descrição | Submit |
| 5 | Progresso, não usado | Value |
| 6 | Máximo de progresso, não usado | Formato |
| 7 | Tipo | Título |
| 8 | Autor | Descrição, opcional em arquivos legados |
| 9 | Pontos | Menor valor é melhor, opcional; qualquer número não zero significa verdadeiro |
| 10–13 | Criação, modificação, votos positivos e negativos; não usados | — |
| 14 | Badge | — |

**Não basta dividir a linha em todos os dois-pontos.** Triggers escritos pelo RAIntegration são
sempre citados: `"M:0xH000001=5"`. Texto comum só precisa de aspas quando contém `:`, aspas,
barra invertida, tabulação ou quebra de linha. Dentro das aspas, `\"`, `\\`, `\n`, `\r` e `\t`
representam os caracteres correspondentes. O tokenizer de referência também remove a barra de
escapes desconhecidos; guardamos o token original para não reescrever esses casos sem edição.
O trigger de conquista não citado tem um caminho legado específico no reader, com uma lista
limitada de flags cujos dois-pontos fazem parte da lógica; não estendemos essa lista por conta
própria. Ao editar esse campo, a escrita passa a usar aspas.

**Editar uma parte não deve apagar as outras.** Cada registro guarda seus tokens originais e os
valores equivalentes do modelo. Um título alterado recebe um token novo, enquanto zeros à esquerda,
escapes antigos e outros campos intactos mantêm seus bytes. As posições de progresso, datas e
votos são preservadas mesmo que o RAIntegration atual as ignore. Qualquer extensão depois do último
campo conhecido é mantida como texto bruto. Linhas `N`, `M`, `BadgeDir=`, desconhecidas e vazias
ficam como registros de passagem, sem API de autoria de notas ou badges nesta etapa.

**O valor mostrado pode ter uma grafia diferente no arquivo.** O RAIntegration completa badges
curtos à esquerda até cinco caracteres. Assim, o modelo lê `123` como `00123`, mas guarda o token
`123` e o reutiliza enquanto o badge não mudar. Um leaderboard com valor numérico `2` em "menor é
melhor" é lido como verdadeiro, mas o token `2` continua intacto; uma edição usa `0` ou `1`.

**Compatibilidade de arquivo difere da validação para publicação.** O formato permite rascunhos
com lógicas ou Value vazios. Pontos são um inteiro sem sinal de 32 bits; o conjunto de pontos
permitido para publicação é uma regra de autoria do editor. Da mesma forma, mantemos o texto de
tipos e formatos desconhecidos: o RAIntegration interpreta tipos desconhecidos numericamente e
formatos desconhecidos como `VALUE`. Isso não significa que sejam opções válidas para uma nova
conquista publicada; a interface da 4.3 deverá oferecer as opções reconhecidas.

**Erros devem bloquear a gravação.** Campos obrigatórios ausentes, aspas não fechadas, inteiros
fora do intervalo, NUL embutido, lógicas inválidas e IDs não zero duplicados do mesmo tipo causam
falha com linha/campo no diagnóstico. O reader de referência é mais permissivo em alguns desses
casos; recusá-los aqui preserva o arquivo para correção, em vez de carregar uma lista parcial e
salvar por cima. Um arquivo existente de zero bytes também é tratado como erro: o RAIntegration o
considera ausente, mas aqui evitamos substituir silenciosamente um arquivo possivelmente truncado.
O limite total é de 16 MiB.

**Troca atômica evita um arquivo pela metade.** Ao carregar, o store fixa um caminho absoluto e
guarda os bytes lidos. Um arquivo ausente cria apenas um documento em memória. Ao salvar:

1. Serializa e valida o documento inteiro antes de escrever.
2. Compara o arquivo atual com o snapshot, recusando alterações ou remoção desde a leitura.
3. Cria um temporário único no mesmo diretório, escreve em laço, sincroniza com `fsync()` e fecha.
4. Compara o snapshot novamente, agora imediatamente antes da substituição.
5. Faz `rename()` e só então atualiza o documento e o snapshot em memória.

Erros removem o temporário e deixam a versão anterior intacta. As permissões comuns do arquivo
existente são preservadas. Links simbólicos no componente final e entradas que não são arquivos
regulares são recusados. A dupla comparação detecta alterações externas usuais, mas não é um lock
compartilhado com outro programa escrevendo exatamente no intervalo final. Também não é uma
promessa de recuperação após qualquer encerramento fatal ou queda de energia.

### Arquivos para ler (nesta ordem)

1. `src/devtools/include/cw/devtools/user_file.hpp`: documento, conquistas e leaderboards.
2. `src/devtools/user_file.cpp`: leitura dos campos, tokens preservados e serialização.
3. `src/devtools/include/cw/devtools/user_file_store.hpp` e `user_file_store.cpp`: estado de leitura
   e substituição atômica.
4. `tests/test_user_file.cpp` e `tests/test_user_file_store.cpp`: exemplos sintéticos e falhas de I/O.
5. `reference/RAIntegration/src/devkit/data/models/GameAssets.cpp`: cabeçalhos, dispatch e IDs.
6. `reference/RAIntegration/src/devkit/data/models/AchievementModel.cpp` e `LeaderboardModel.cpp`:
   ordem dos campos e valores padrão.
7. `reference/RAIntegration/src/devkit/data/models/AssetModelBase.cpp` e
   `reference/RAIntegration/src/devkit/util/Tokenizer.cpp`: aspas e escapes.
8. `reference/RAIntegration/src/services/impl/FileLocalStorage.cpp`: caminho do RACache.
9. `third_party/rcheevos/src/rcheevos/value.c` e `format.c`: Value e formatos da versão compilada.

### O que aprendemos com a referência

- O writer da referência reconstrói o arquivo e zera campos que não usa. Nosso editor ainda
  não conhece todos os registros, então precisa preservar essas informações separadamente.
- Leaderboards legados podem omitir descrição e a opção de menor valor. A leitura assume vazio
  e falso; a serialização mantém essa omissão enquanto esses campos não forem editados.
- Um trigger válido não basta para validar Value. `M:0xH000001` é uma expressão de valor válida,
  embora a 4.1 recuse essa forma sem comparação como trigger de conquista. Usamos o parser de
  valores do rcheevos e verificamos o consumo de todo o campo.
- O RAIntegration escreve CRLF, e seu reader também aceita LF. Preservamos os terminadores de
  cada linha; novas linhas seguem o estilo do cabeçalho, e a ausência de quebra no fim é mantida.

### Problemas encontrados

- **Caminho relativo não pode trocar de destino depois do load:** o store converte para absoluto
  antes de ler. Um teste muda o diretório corrente entre leitura e escrita e confirma o destino.
- **Última linha sem terminador:** ao inserir outro registro, é necessário acrescentar um
  separador; caso contrário, duas linhas virariam uma. Novos registros também herdam CRLF em
  arquivos desse estilo. A regressão falhou antes da correção e passou depois.
- **Cabeçalho vazio no fim:** apagar o título de um arquivo que terminava imediatamente após o
  título precisa emitir uma segunda linha vazia, não um arquivo com só o cabeçalho de versão.
- **Testar recusa antes de escrever não exercita uma falha de escrita:** um processo filho limita
  seu tamanho máximo de arquivo a 64 bytes e ignora `SIGXFSZ` somente nele. A escrita parcial
  retorna erro e o teste confirma bytes antigos intactos e ausência de temporários. Esse teste
  verifica o caminho de erro de I/O; não instala um tratador global de sinais no aplicativo.
- **Linhas opacas não são cabeçalhos:** aplicar a mesma validação de cabeçalho a um registro
  desconhecido rejeitava um retorno de carro interno que o parser havia preservado. A linha
  de passagem agora é emitida pelos seus bytes originais, sem interpretar esse conteúdo.

### Verificação

Os **189 casos e 8.841 asserções** passaram, incluindo as regressões da 4.1 e os 28 casos novos
desta etapa. Build e formatação passaram sem avisos. A execução com AddressSanitizer e
UndefinedBehaviorSanitizer também passou; LeakSanitizer foi desativado por limitação do ambiente.
Os testes usam somente texto, memória e arquivos sintéticos em diretórios temporários.

A conferência do formato foi feita nos fontes reais de leitura/escrita do RAIntegration e no
rcheevos v12.5.0 compilado; não foi executado o aplicativo Windows para uma validação manual de
importação. Um teste de mutação desativou temporariamente a proteção de alterações externas:
o teste falhou. Após restaurar o código, a suíte completa voltou a passar.

Não há caminho novo na interface para testar manualmente nesta etapa. Para repetir os checks,
use `cmake -S . -B build`, `cmake --build build -j` e
`ctest --test-dir build --output-on-failure`.

### Para fixar

1. Por que `split(':')` não consegue ler um registro com lógica Measured e título com dois-pontos?
2. Qual é a diferença entre ID do asset, subset ID e game ID no nome do arquivo?
3. Por que guardar só os campos que aparecem no editor poderia apagar notas ou extensões?
4. Por que o store precisa recusar save depois de um load inválido, mesmo recebendo um documento novo?
5. Qual é a diferença entre substituição atômica e impedir qualquer escrita concorrente?
6. Exercício: carregue uma fixture sintética com uma conquista e uma linha desconhecida; altere
   os pontos para 10, salve e confirme que só o campo editado mudou e a linha desconhecida ficou intacta.
