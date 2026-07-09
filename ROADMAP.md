# Roadmap — Pesquisa e integração Git no painel lateral

## Visão

Oferecer pesquisa e integração Git nativas, leves e robustas, preservando a simplicidade, baixo consumo de memória e resposta imediata do Pluma.

O painel lateral terá itens independentes para Documentos, Navegador de arquivos, Pesquisa, Controle de código-fonte e Histórico Git. Pesquisa e Git não devem depender de diálogos externos ou terminais para operações comuns.

## Princípios e Filosofia de Leveza

O Pluma prioriza a **baixa utilização de memória, inicialização rápida e resposta imediata da interface**. Novas funcionalidades devem preservar essas características, evitando processos residentes, monitoramento contínuo do sistema de arquivos, caches extensivos e estruturas que aumentem significativamente o consumo de recursos.

### Diretrizes de evolução:
- **Nenhuma thread residente** apenas para atualizar a interface gráfica.
- **Nenhum monitoramento contínuo** de arquivos, Git ou busca (com exceção do refresh sob demanda ou batching muito controlado).
- **Nenhum cache volumoso** apenas para acelerar recursos pouco utilizados.
- **Nenhum recurso** que aumente perceptivelmente o tempo de inicialização do editor.
- **Acionamento sob demanda:** Funcionalidades novas complexas devem ser acionadas apenas sob demanda do usuário.
- **Simplicidade:** Preferir algoritmos simples e previsíveis em vez de soluções mais sofisticadas que aumentem o consumo de recursos.

### Princípios de desenvolvimento:
- Interface nativa, assíncrona e cancelável.
- Pesquisa, arquivos, alterações, histórico e referências usam modelos separados.
- Nenhuma operação destrutiva ocorre sem confirmação contextual.
- Buffers não salvos nunca são sobrescritos silenciosamente.
- Git e ripgrep são dependências opcionais detectadas pelo Meson.
- Alertas de dependência são agnósticos de distribuição.
- Comandos usam argumentos estruturados, nunca concatenação de shell.
- Parsing Git usa formatos estáveis e locale controlado.
- Autenticação permanece sob responsabilidade dos mecanismos seguros do Git.

---

# 1. Fundação comum

## 1.1 Navegação lateral

- [x] Criar itens separados para Arquivos, Pesquisa e Git.
- [x] Usar ícones simbólicos compatíveis com temas claro/escuro.
- [ ] Adicionar tooltips, nomes acessíveis e navegação por teclado.
- [ ] Preservar item ativo, largura e estado recolhido por janela.
- [ ] Usar cabeçalhos compactos com ações contextuais.
- [x] Evitar uma lista única misturando arquivos, commits, branches e conflitos.

## 1.2 Modelo de projeto

- [x] Compartilhar a raiz entre Navegador, Pesquisa e Git.
- [x] Usar a pasta passada por `pluma .` como raiz.
- [ ] Permitir trocar a raiz pelo Navegador de arquivos.
- [x] Detectar o repositório ancestral da raiz ou documento ativo.
- [ ] Atualizar todos os painéis quando a raiz mudar.
- [ ] Exibir claramente projeto e repositório ativos.

---

# 2. Pesquisa global

## 2.1 Experiência de pesquisa

- [x] Abrir e focar Pesquisa com `Ctrl+Shift+F`.
- [x] Manter `Ctrl+F` para o documento atual.
- [x] Remover o conflito com a ferramenta externa baseada em `zenity`.
- [x] Adicionar campos de pesquisa e substituição.
- [x] Adicionar opções de caixa, palavra inteira e regex.
- [ ] Adicionar filtros recolhíveis de inclusão e exclusão.
- [ ] Respeitar `.gitignore` por padrão.
- [x] Permitir incluir arquivos ignorados.
- [ ] Mostrar progresso, duração, cancelamento e erros de regex inline.
- [x] Usar ripgrep com fallback funcional.

## 2.2 Resultados

- [ ] Agrupar por arquivo, mostrando quantidade por grupo e total.
- [ ] Exibir caminho relativo, linha, coluna e trecho destacado.
- [x] Expandir/recolher arquivos ou todos os resultados.
- [x] Abrir resultado com mouse ou teclado.
- [ ] Reutilizar documentos abertos.
- [ ] Copiar texto, caminho ou localização pelo menu de contexto.

## 2.3 Substituição segura

- [x] Suportar grupos de captura regex.
- [x] Preservar codificação, final de linha e permissões.
- [x] Gravar arquivos fechados atomicamente.
- [x] Confirmar alterações globais e resumir falhas.

---

# 3. Backend Git

## 3.1 Infraestrutura

- [x] Detectar Git no Meson como dependência opcional.
- [x] Criar executor assíncrono baseado em `GSubprocess`.
- [x] Cancelar operações ao fechar janela ou trocar repositório.
- [ ] Controlar concorrência entre comandos incompatíveis.
- [x] Monitorar working tree periodicamente e observar `.git`, `HEAD` e refs.
- [x] Agrupar eventos para evitar atualizações excessivas.
- [x] Atualizar após operações internas ou externas.
- [ ] Suportar repositórios sem commits, worktrees, submódulos e bare.
- [x] Mostrar erros acionáveis no próprio painel.

## 3.2 Modelo de estado

- [x] Interpretar `git status --porcelain=v2` com caminhos preservados.
- [x] Representar index e working tree separadamente.
- [x] Identificar modificados, adicionados, removidos e renomeados.
- [x] Identificar não rastreados e conflitos.
- [x] Calcular branch, upstream e ahead/behind.
- [x] Não depender de texto traduzido produzido pelo Git.

---

# 4. Source Control

## 4.1 Visão de alterações

- [x] Criar item lateral “Controle de código-fonte”.
- [x] Mostrar branch e upstream no cabeçalho.
- [x] Separar Conflitos, Staged, Alterações e Não rastreados.
- [x] Exibir códigos e contadores de status por arquivo/grupo.
- [ ] Ordenar por status, caminho ou nome.
- [x] Filtrar alterações por texto.
- [x] Atualizar automaticamente após salvar ou executar Git.
- [x] Exibir branch, alterações e ahead/behind no painel e na barra de status.

## 4.2 Stage, unstage e descarte

- [x] Stage de arquivo individual ou todos e unstage individual.
- [x] Stage/unstage de hunk.
- [x] Stage/unstage de linhas selecionadas.
  - `pluma_git_diff_hunk_subset()` (`pluma-git-diff.c`/`.h`) reconstrói um
    hunk mantendo só um subconjunto das linhas `+`/`-` escolhidas pelo
    usuário (linhas de contexto sempre ficam), recalculando o cabeçalho
    `@@ -a,b +c,d @@` a partir da contagem real do corpo reconstruído —
    é a mesma regra do `git add --patch`: no sentido de aplicação
    escolhido (stage = direto, unstage/descarte = `--reverse`), uma linha
    do símbolo "que representa a mudança" nessa direção que não foi
    selecionada é **descartada** do patch; uma linha do símbolo oposto
    não selecionada vira **contexto** (permanece, já que seu estado não
    está sendo tocado).
  - Diálogo de escolha de hunk (`choose_hunk_patch`) ganhou uma lista de
    checkboxes por linha do hunk selecionado, todas marcadas por padrão
    (deixar tudo marcado reproduz o comportamento antigo de hunk inteiro).
  - 6 testes novos em `tests/git-diff.c` cobrindo contagem de linhas,
    seleção total (deve reproduzir o hunk original), stage parcial,
    unstage parcial, nada selecionado (retorna NULL) e preservação do
    cabeçalho compartilhado do arquivo.
  - Validado também contra `git apply` de verdade num repositório
    temporário (não só contra as próprias expectativas dos testes): os
    patches parciais gerados (stage e unstage/`--reverse`) aplicaram e
    produziram exatamente o conteúdo de arquivo esperado.
- [x] Descartar alteração com confirmação.
- [x] Exigir confirmação reforçada para excluir não rastreados.
- [x] Restaurar arquivo removido pelo descarte da working tree.
- [x] Abrir arquivo pelo painel.
- [ ] Mostrar progresso e bloquear ações concorrentes incompatíveis.

---

# 5. Diff integrado

- [x] Abrir diff textual ao ativar arquivo alterado.
- [ ] Oferecer visualização inline e lado a lado.
- [x] Diferenciar adições, remoções e modificações pela saída Git com destaque de sintaxe.
  - Cobria só o diff unificado (via linguagem GtkSourceView "diff", dependente
    do esquema de cores escolhido). History, Branches, Tags, Remotes e
    Stashes não tinham nenhuma diferenciação visual. Trocado por
    `apply_git_output_colors()` (`pluma-git-panel.c`) — tags GtkTextTag
    aplicadas diretamente (cientes de tema claro/escuro via `is_dark_theme()`,
    já usado pela visão lado a lado), cobrindo as 6 views: diff (+/-/@@/
    cabeçalho), hash de commit, nomes de ref/branch/tag/remote e marcador de
    branch atual. Não depende mais do esquema de cores nem da preferência
    global de "realce de sintaxe".
  - "View Diff" (antes "Diff selected file") agora também aceita selecionar
    a raiz de "Changes" ou "Staged Changes" pra ver o diff de todos os
    arquivos daquele grupo de uma vez, mantendo o comportamento por arquivo
    quando um arquivo específico é selecionado.
- [ ] Exibir números de linha antigos e novos.
- [ ] Navegar entre hunks.
- [x] Stage, unstage ou descartar hunk.
- [x] Stage, unstage ou descartar linhas selecionadas. (ver seção 4.2 — mesmo diálogo de escolha de hunk, `choose_hunk_patch`/`pluma_git_diff_hunk_subset`, cobre as três operações já que discard também usa `--reverse`.)
- [x] Comparar working tree com index e index com `HEAD`.
- [x] Comparar arquivo ou commit com outra referência.
- [x] Detectar arquivos binários.
- [ ] Atualizar diff após salvar ou alterar o index.
- [x] Indicar claramente os lados working tree, index e commit.

---

# 6. Commit

- [x] Adicionar campo para mensagem de commit.
- [x] Validar mensagem e exibir contador configurável.
- [x] Criar commit somente com staged.
- [x] Oferecer “stage all and commit” separadamente.
- [x] Suportar amend com confirmação.
- [x] Suportar sign-off e assinatura configurada no Git.
- [x] Executar hooks do Git e mostrar falhas.
- [x] Preservar mensagem quando houver falha.
- [x] Limpar mensagem somente após sucesso.
- [ ] Mostrar o commit criado e atualizar status/log.

---

# 7. Branches, tags e referências

- [ ] Criar visão separada “Branches e Tags”.
- [ ] Listar branches locais/remotas e upstream.
- [x] Criar branch a partir da referência atual.
- [x] Trocar branch delegando ao Git a validação de alterações pendentes.
- [x] Excluir branch local com confirmação.
- [x] Publicar branch e configurar upstream.
- [x] Listar tags leves/anotadas.
- [x] Criar tags.
- [x] Avisar claramente ao entrar em detached HEAD.
- [ ] Filtrar e pesquisar referências.

---

# 8. Remotes e sincronização

- [ ] Criar visão separada “Remotes”.
- [x] Listar URLs de fetch e push.
- [x] Adicionar e remover remote.
- [x] Fetch de todos com prune.
- [x] Pull segundo a configuração do Git.
- [x] Push da branch atual.
- [ ] Prune com confirmação.
- [x] Mostrar falhas de transferência no painel.
- [x] Nunca armazenar credenciais no Pluma.

---

# 9. Histórico e log

- [ ] Criar visão separada “Histórico”.
- [x] Mostrar grafo textual, hash, refs e mensagem em aba integrada.
- [ ] Paginar incrementalmente.
- [ ] Filtrar por texto, autor, data, branch e caminho.
- [ ] Mostrar detalhes e arquivos de um commit.
- [ ] Abrir diff completo do commit.
- [ ] Copiar hash/mensagem e criar branch/tag.
- [x] Reverter com confirmação e executar cherry-pick, expondo conflitos no Source Control.
- [x] Comparar duas referências.
- [x] Mostrar histórico do arquivo atual com `--follow`.

---

# 10. Blame

Implementado do zero (não existia nenhum código antes desta sessão).

- [x] Executar blame do arquivo atual sob demanda.
  - `blame_current_file_clicked` (menu "More" → "Blame Current File"),
    roda `git blame --porcelain -- <arquivo>` de forma síncrona
    (`get_git_output`), mesmo padrão já usado por `open_side_by_side_diff`.
- [x] Mostrar autor, data e commit sem alterar o texto.
  - Aba somente-leitura própria (`show_read_only_git_tab`, extraído de
    `call_done` e reaproveitado por History/Branches/Tags/Remotes/Stashes/
    diffs também), colorida via `apply_git_output_colors` (novo caso
    `GIT_OUTPUT_BLAME`: hash e marcador "(uncommitted)").
- [x] Usar o documento ativo ao executar blame.
- [x] Abrir commit associado à linha.
  - A tag `git-hash` (usada em Blame, History, Branches e Stashes) agora
    fica sublinhada e clicável: `on_git_hash_tag_event` (sinal `"event"`
    da própria `GtkTextTag`) extrai o hash sob o clique via
    `gtk_text_iter_backward/forward_to_tag_toggle` e abre
    `git show <hash>` numa aba "Diff: <hash>" (reaproveita a coloração de
    diff já existente). Cursor vira ponteiro ao passar por cima
    (`on_git_output_motion`, `motion-notify-event` na view). Clicar num
    hash todo-zero (linha não commitada) mostra aviso em vez de tentar
    `git show` num commit inexistente. Beneficia não só Blame como
    History/Branches/Stashes ao mesmo tempo, já que todas usam a mesma
    tag.
- [x] Mostrar detalhes em tooltip/popover.
  - `on_blame_query_tooltip` (sinal `"query-tooltip"` na view, ativado só
    para abas de Blame via `gtk_widget_set_has_tooltip`) mostra, ao
    passar o mouse numa linha: hash, autor completo + e-mail, data e hora
    exatas (`%Y-%m-%d %H:%M:%S`, não só a data curta da view principal) e
    a mensagem de resumo do commit — dado que já existia no
    `PlumaGitBlameLine` mas não cabia na linha formatada. A lista
    (`GPtrArray`) usada pra montar o texto é anexada à própria view via
    `g_object_set_data_full` (dono passa a ser a view, liberada
    automaticamente com ela) e o handler indexa direto pela linha do
    buffer sob o cursor, já que existe uma linha de texto por
    `PlumaGitBlameLine` na mesma ordem.
  - Novo campo `author_mail` no parser (`author-mail <...>` do
    `--porcelain`, não capturado antes) — cacheado por hash do mesmo jeito
    que author/summary, incluindo o caso de reaproveitamento na forma
    compacta. 2 asserts novos em `tests/git-blame.c` cobrindo isso
    (primeira ocorrência e forma compacta).
- [x] Tratar linhas ainda não commitadas.
  - Marcadas como "(uncommitted)" em vez de data, em vez de mostrar o
    hash `000...0` do Git como se fosse um commit real; clicar nesse hash
    não tenta abrir um commit inexistente (ver item acima); o tooltip
    mostra "Not committed yet" em vez de autor/data/mensagem fictícios.

Com isso, **a seção 10 (Blame) está com todos os itens concluídos.**

Parser (`pluma-git-blame.c`/`.h`, `pluma_git_blame_parse`) testado com
saída real de `git blame --porcelain` capturada de um repositório de
teste (não texto inventado à mão) — 5 testes em `tests/git-blame.c`
cobrindo: entrada vazia, metadados (incl. e-mail) na primeira ocorrência
de um commit, reaproveitamento correto de metadados (incl. e-mail) na
forma compacta (quando o Git repete um commit já visto, ele omite
author/summary/etc. e só repete o cabeçalho curto — o parser precisa
cachear por hash), detecção de linha não commitada (hash todo zero) e
confirmação de que uma linha commitada normal não é marcada como não
commitada.

Nota: o clique no hash (padrão GTK3 de "tag clicável" via sinal `"event"`
da `GtkTextTag`, mesma técnica do `gtk3-demo` de hypertext) e o tooltip
(`"query-tooltip"`, também API GTK3 padrão) compilam sem warnings e
seguem a API documentada, mas nenhum dos dois foi testado visualmente de
verdade (clicando/passando o mouse na interface) — não havia
Xvfb/xdotool disponíveis para testar isolado da tela real, e captura de
tela da sessão real do usuário está fora de cogitação sem permissão
explícita por uso. Vale uma passagem manual do usuário antes de
considerar 100% validado.

---

# 11. Conflitos e merge

- [x] Criar visão/grupo dedicado de conflitos.
- [x] Detectar estados unmerged pelo index.
- [ ] Marcar resolvido somente por stage explícito.
- [ ] Tratar conflitos de adição, remoção e renomeação.
- [x] Mostrar conflitos de merge, rebase, cherry-pick e revert no grupo dedicado.
- [x] Continuar merge/rebase e abortar merge/rebase/cherry-pick com confirmação destrutiva.

---

# 12. Stash e limpeza

- [x] Listar stashes em aba integrada.
- [x] Criar stash incluindo arquivos não rastreados.
- [x] Pop do stash mais recente.
- [x] Visualizar diff do stash.
- [ ] Pré-visualizar arquivos antes de `git clean`.
- [ ] Exigir seleção e confirmação para limpeza.

---

# 13. Operações avançadas

- [x] Merge e rebase de referência informada.
- [x] Cherry-pick de commit informado.
- [x] Revert de commit com confirmação.
- [ ] Reset soft, mixed e hard com confirmações distintas.
- [ ] Recuperação de operações interrompidas.

Operações destrutivas avançadas só serão entregues após testes específicos de recuperação.

---

# 14. Integração entre Arquivos, Pesquisa e Git

- [ ] Abrir diff, histórico e blame pelo menu do arquivo.
- [ ] Filtrar Pesquisa por arquivos alterados.
- [ ] Pesquisar em staged, working tree ou commit selecionado.
- [ ] Revelar resultado no Navegador ou Source Control.

---

# 15. Segurança, desempenho e acessibilidade

## Segurança

- [ ] Classificar centralmente operações destrutivas.
- [x] Informar arquivos e referências nas confirmações implementadas.
- [x] Bloquear operações incompatíveis com buffers não salvos.
- [x] Tratar caminhos com espaços, hífen, Unicode e bytes inválidos.
- [x] Não armazenar nem registrar tokens ou credenciais.
- [ ] Oferecer recuperação clara após falhas parciais.

## Desempenho

- [ ] Evitar varreduras completas após cada evento.
- [x] Cancelar tarefas ao destruir o painel e agrupar atualizações de status.
- [ ] Configurar limites de resultados, tamanho e histórico.
- [ ] Testar monorepos e milhares de alterações.

## Acessibilidade e localização

- [ ] Garantir operação completa por teclado.
- [x] Definir nomes acessíveis nos controles principais.
- [ ] Suportar alto contraste e escala.
- [ ] Tornar todas as strings traduzíveis.
- [ ] Formatar datas pelo locale sem afetar parsing.

---

# 16. Testes

- [x] Parser de `status --porcelain=v2 -z`.
- [ ] Parsers de refs, log, diff e blame.
- [x] Repositório inicial e commits normais em fixture isolada.
- [ ] Testes com bare repository.
- [ ] Stage/unstage por arquivo, hunk e linha.
- [x] Merge, conflito e abort em fixture isolada.
- [ ] Pesquisa com e sem ripgrep.
- [ ] Ambiente sem Git.
- [ ] Cancelamento e fechamento de janela.
- [ ] Caminhos incomuns e arquivos binários.
- [ ] Navegação por teclado e acessibilidade.
- [x] Fixtures isoladas; nunca usar o repositório real nos testes.

---

# Marcos

## Prioridade imediata — integridade de dados

Reavaliado em auditoria de código (item a item, contra `pluma-git-panel.c`,
`pluma-git-status-parser.c`, `pluma-git-diff.c`, `pluma-git-commit.c` e
`tests/`). Os itens 2 e 3 abaixo, como estavam escritos antes, já refletiam
trabalho concluído e não a prioridade real; a ordem foi corrigida.

1. **Testes dos parsers Git — parcialmente feito**
   - `status --porcelain=v2 -z`: coberto (`tests/git-status-parser.c` +
     `tests/git-integration.sh`), incluindo espaços, hífen, Unicode, bytes
     UTF-8 inválidos, renomeios e conflitos.
   - diff/hunk: **feito.** `choose_hunk_patch` tinha a lógica de split de
     hunk (puro parsing) misturada com código GTK (diálogo modal), sem
     nenhum teste. Extraída para `pluma_git_diff_split_hunks()`
     (`pluma-git-diff.c`/`.h`, retorna `PlumaGitHunk{label,patch}`);
     `choose_hunk_patch` agora só monta o diálogo em cima do resultado.
     4 testes novos em `tests/git-diff.c`: diff vazio/NULL, diff só com
     cabeçalho (sem `@@`, ex. rename puro) sem gerar hunk fantasma, hunk
     único, e múltiplos hunks cada um carregando o cabeçalho completo
     (`diff --git`/`index`/`---`/`+++`) sem vazar linhas de outro hunk —
     é exatamente isso que o `git apply` precisa para aplicar corretamente.
   - refs e log: **não é um gap de teste, é feature não implementada.**
     Investigação confirmou que não existe parser de refs/log hoje —
     `git log`/`git branch`/`git tag`/`git remote` têm a saída bruta
     jogada direto num buffer de texto (`history_clicked`,
     `current_file_history_clicked`, `branches_clicked`, `tags_clicked`,
     `remotes_clicked`, todas em `pluma-git-panel.c` por volta de
     `:1446`-`:1476`), sem parsing em campos estruturados. "Testar o
     parser" não faz sentido até existir parser — isso é trabalho das
     seções 7 (Branches/tags), 8 (Remotes) e 9 (Histórico), que já têm
     itens `[ ]` para "criar visão separada" com dados estruturados
     (branch, autor, data, hash). Tratar como consequência dessas
     seções, não como item de teste isolado.
   - blame: não há o que testar — ver seção 10, funcionalidade não existe.
2. **Proteção de buffers não salvos — já feito para tudo que existe hoje**
   - `ensure_documents_saved()` (`pluma-git-panel.c:151`) já cobre discard,
     descarte de hunk, pull, stash pop, switch, merge, rebase, cherry-pick,
     revert e abort (10 pontos de chamada).
   - `git reset --hard` e `git clean` continuam não implementados (seções
     12/13) — nada a proteger ainda; proteger no momento em que forem
     implementados, não antes.
3. **Operações por hunk e linha — feito, hunk e linha**
   - Stage/unstage/descarte por hunk via `git apply --cached`/`--reverse`
     (`apply_selected_hunk`), delegando a aplicação do patch ao próprio
     Git em vez de um parser de patch caseiro.
   - Stage/unstage/descarte por linha via `pluma_git_diff_hunk_subset()`
     (seção 4.2) — testado contra os próprios testes e contra `git apply`
     real num repositório temporário.
   - Descarte continua exigindo confirmação (`confirm_action`) tanto no
     nível de hunk inteiro quanto no de linhas selecionadas dentro dele.

Critério de saída: nenhuma operação dessas etapas pode modificar o repositório sem teste de sucesso, falha e recuperação correspondente.

## Marco A — Pesquisa integrada

Painel nativo, atalho, filtros, resultados, navegação e substituição segura, sem `zenity` em `Ctrl+Shift+F`.

## Marco B — Source Control essencial

Status, diff, stage, unstage, descarte, commit, atualização automática e barra de status.

## Marco C — Navegação Git

Branches, tags, remotes, fetch/pull/push e histórico paginado.

## Marco D — Colaboração

Blame, stash, conflitos, merge, rebase, cherry-pick e revert.

## Marco E — Paridade avançada

Stage por linha, editor de merge, worktrees, submódulos, desempenho, acessibilidade e cobertura de testes.

---

# Definição de pronto

1. Pesquisa, Arquivos e Git são áreas independentes e consistentes.
2. `Ctrl+Shift+F` nunca abre uma janela externa.
3. Status, diff, stage, unstage, descarte e commit são seguros e testados.
4. Branches, tags, remotes, log, blame, stash e conflitos têm visões próprias.
5. Operações longas são assíncronas, canceláveis e mostram progresso.
6. Operações destrutivas têm prévia ou confirmação contextual.
7. Buffers não salvos nunca são perdidos silenciosamente.
8. Toda a interface funciona por teclado e com tecnologias assistivas.
9. Dependências opcionais ausentes geram alertas agnósticos e não quebram o build.
10. Testes cobrem estados normais, falhas e recuperação.

A maturidade das novas funcionalidades será medida pela sua corretude, robustez e impacto nulo no desempenho geral do Pluma.
