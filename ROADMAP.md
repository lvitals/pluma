# Roadmap — Pesquisa e integração Git no painel lateral

## Visão

Oferecer no Pluma uma experiência integrada de pesquisa e controle de código-fonte comparável aos fluxos do Visual Studio Code, preservando GTK 3, MATE, acessibilidade e a arquitetura do projeto.

O painel lateral terá itens independentes para Documentos, Navegador de arquivos, Pesquisa, Controle de código-fonte e Histórico Git. Pesquisa e Git não devem depender de diálogos externos ou terminais para operações comuns.

## Princípios

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
- [ ] Exibir badges de resultados e alterações.
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
- [ ] Manter histórico de consultas e substituições.
- [ ] Pesquisar automaticamente após debounce configurável.
- [ ] Mostrar progresso, duração, cancelamento e erros de regex inline.
- [x] Usar ripgrep com fallback funcional.

## 2.2 Resultados

- [ ] Agrupar por arquivo, mostrando quantidade por grupo e total.
- [ ] Exibir caminho relativo, linha, coluna e trecho destacado.
- [x] Expandir/recolher arquivos ou todos os resultados.
- [x] Abrir resultado com mouse ou teclado.
- [ ] Exibir prévia sem criar aba definitiva.
- [ ] Reutilizar documentos abertos.
- [ ] Atualizar após salvar, criar, mover ou remover arquivos.
- [ ] Copiar texto, caminho ou localização pelo menu de contexto.

## 2.3 Substituição segura

- [ ] Substituir ocorrência, arquivo ou projeto.
- [ ] Excluir resultados individuais antes de aplicar.
- [ ] Mostrar diff/prévia da substituição global.
- [x] Suportar grupos de captura regex.
- [ ] Integrar documentos abertos ao histórico de desfazer.
- [ ] Preservar codificação, final de linha e permissões.
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
- [ ] Stage/unstage de linhas selecionadas.
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
- [ ] Exibir números de linha antigos e novos.
- [ ] Navegar entre hunks.
- [x] Stage, unstage ou descartar hunk.
- [ ] Stage, unstage ou descartar linhas selecionadas.
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
- [ ] Sincronizar pull/push como ação explícita.
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

- [x] Executar blame do arquivo atual sob demanda.
- [x] Mostrar autor, data e commit sem alterar o texto.
- [x] Usar o documento ativo ao executar blame.
- [ ] Abrir commit associado à linha.
- [ ] Mostrar detalhes em tooltip/popover.
- [ ] Tratar linhas ainda não commitadas.

---

# 11. Conflitos e merge

- [x] Criar visão/grupo dedicado de conflitos.
- [x] Detectar estados unmerged pelo index.
- [ ] Criar editor com Base, Current, Incoming e Result.
- [ ] Aceitar Current, Incoming, ambos ou edição manual.
- [ ] Navegar entre conflitos.
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
- [ ] Rebase interativo com editor de sequência seguro.
- [x] Cherry-pick de commit informado.
- [x] Revert de commit com confirmação.
- [ ] Reset soft, mixed e hard com confirmações distintas.
- [ ] Submódulos: status, init, update e sync.
- [ ] Múltiplos worktrees.
- [ ] Recuperação de operações interrompidas.

Operações destrutivas avançadas só serão entregues após testes específicos de recuperação.

---

# 14. Integração entre Arquivos, Pesquisa e Git

- [ ] Exibir decoração Git no Navegador de arquivos.
- [ ] Abrir diff, histórico e blame pelo menu do arquivo.
- [ ] Filtrar Pesquisa por arquivos alterados.
- [ ] Pesquisar em staged, working tree ou commit selecionado.
- [ ] Revelar resultado no Navegador ou Source Control.
- [ ] Atualizar badges Git após substituição global.
- [ ] Comparar resultados antes/depois da substituição.

---

# 15. Segurança, desempenho e acessibilidade

## Segurança

- [ ] Classificar centralmente operações destrutivas.
- [x] Informar arquivos e referências nas confirmações implementadas.
- [x] Bloquear operações incompatíveis com buffers não salvos.
- [ ] Tratar caminhos com espaços, hífen, Unicode e bytes inválidos.
- [x] Não armazenar nem registrar tokens ou credenciais.
- [ ] Oferecer recuperação clara após falhas parciais.

## Desempenho

- [ ] Processar status, log e pesquisa incrementalmente.
- [ ] Evitar varreduras completas após cada evento.
- [ ] Virtualizar listas grandes.
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
- [ ] Worktrees, submódulos e bare repository.
- [ ] Stage/unstage por arquivo, hunk e linha.
- [x] Merge, conflito e abort em fixture isolada.
- [ ] Substituição integrada ao status Git.
- [ ] Pesquisa com e sem ripgrep.
- [ ] Ambiente sem Git.
- [ ] Cancelamento e fechamento de janela.
- [ ] Caminhos incomuns e arquivos binários.
- [ ] Navegação por teclado e acessibilidade.
- [x] Fixtures isoladas; nunca usar o repositório real nos testes.

---

# Marcos

## Prioridade imediata — integridade de dados

Executar nesta ordem antes de ampliar as operações Git avançadas:

1. **Testes dos parsers Git**
   - Cobrir `status --porcelain=v2 -z`, refs, log, diff e blame.
   - Incluir caminhos com espaços, hífen, Unicode e bytes inválidos.
   - Incluir arquivos binários, repositórios sem commits e estados de conflito.
2. **Proteção de buffers não salvos**
   - Identificar operações capazes de substituir, remover ou trocar arquivos abertos.
   - Bloquear a operação ou exigir que o usuário salve/descartar alterações explicitamente.
   - Cobrir checkout, switch, merge, rebase, pull, restore, reset e limpeza.
3. **Operações por hunk e linha**
   - Implementar parser e aplicação segura de patches antes da interface de stage/unstage.
   - Adicionar stage, unstage e descarte por hunk.
   - Somente depois habilitar operações por linhas selecionadas.
   - Exigir prévia, confirmação para descarte e testes de recuperação.

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

“Paridade com VS Code” será medida pelos fluxos e critérios acima, não por cópia visual literal ou pela arquitetura de extensões do VS Code.
