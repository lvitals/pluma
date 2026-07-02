# Plugins do Pluma: C vs Python e plano de porte para Lua

Este documento levanta em que linguagem cada plugin do Pluma está escrito hoje
e descreve um plano para portar os plugins em Python para Lua.

## 1. Visão geral

| Plugin | Linguagem | Arquivos-chave | LOC aprox. |
|---|---|---|---|
| docinfo | C | `docinfo.c/.h` | pequeno |
| modelines | C | `pluma-modeline-plugin.c/.h`, `modeline-parser.c/.h` | pequeno |
| sort | C | `sort.c/.h` | pequeno |
| spell | C | 7 arquivos `.c`/`.h` | médio |
| taglist | C | 3 arquivos `.c`/`.h` | médio |
| time | C | `time.c/.h` | pequeno |
| trailsave | C | `trailsave.c/.h` | pequeno |
| pythonloader | C (infraestrutura) | integra libpeas ao Python 3 via PyGObject | 703 |
| **externaltools** | **Python** | `tools/*.py` (manager, library, capture, outputpanel…) | ~2.734 |
| **pythonconsole** | **Python** | `pythonconsole/*.py` | ~610 |
| **quickopen** | **Python** | `quickopen/*.py` | ~908 |
| **snippets** | **Python** | `snippets/*.py` (Parser, Completion, Manager…) | ~5.680 |

Sete plugins (`docinfo`, `modelines`, `sort`, `spell`, `taglist`, `time`,
`trailsave`) já estão em C e carregam diretamente via o loader C nativo do
libpeas — não precisam de porte. Quatro plugins estão em Python:
`externaltools`, `pythonconsole`, `quickopen` e `snippets`.

## 2. Por que Lua é viável

O Pluma depende de `libpeas-1.0 >= 1.2.0` (a versão instalada no sistema de
desenvolvimento é 1.38.1). O pacote `libpeas` do Arch já traz um **loader Lua
5.1 nativo**:

```
/usr/lib/libpeas-1.0/loaders/liblua51loader.so
```

Esse loader é reconhecido pelos arquivos `.plugin` via `Loader=lua5.1` (assim
como os plugins Python usam `Loader=python3`). A dependência opcional
documentada pelo pacote é `lua51-lgi: Lua loader` — [LGI](https://github.com/lgi-devs/lgi)
é o binding de Lua para GObject Introspection, o equivalente Lua do
PyGObject usado hoje pelos plugins Python.

Consequência prática: **não é necessário escrever um loader C novo**, como foi
feito para Python em `plugins/pythonloader/`. O trabalho de porte se resume a:

- instalar `lua51-lgi` (e a lib `liblua5.1` da qual ele depende) como
  dependência de build/runtime;
- trocar `Loader=python3` por `Loader=lua5.1` nos `.plugin.desktop.in.in` dos
  plugins portados;
- reescrever o código de cada plugin de Python para Lua, mantendo a mesma
  API de GObject Introspection (Gtk, Gio, GLib, Pluma, PeasGtk) que já é usada
  hoje via PyGObject.

**Nota de ambiente:** no momento da análise, nem `liblua5.1` nem `lua51-lgi`
estavam instalados no sistema (apenas Lua 5.4/5.5). Os dois pacotes estão
disponíveis no repositório `extra` do Arch (`lua51-lgi`), mas precisam ser
adicionados explicitamente às dependências de build antes de iniciar o porte.

### Padrão de binding: PyGObject vs LGI

Os plugins Python seguem o padrão idiomático de GObject Introspection:

```python
from gi.repository import GObject, Peas, Pluma

class QuickOpenPlugin(GObject.Object, Pluma.WindowActivatable):
    __gtype_name__ = "QuickOpenPlugin"
    window = GObject.Property(type=Pluma.Window)

    def do_activate(self):
        ...
    def do_deactivate(self):
        ...
    def do_update_state(self):
        ...
```

O equivalente em Lua/LGI usa `GObject.Object:derive(...)` para registrar uma
nova GType e implementar as mesmas interfaces:

```lua
local lgi = require('lgi')
local GObject = lgi.GObject
local Pluma = lgi.Pluma

local QuickOpenPlugin = GObject.Object:derive('QuickOpenPlugin', {Pluma.WindowActivatable})
QuickOpenPlugin._property.window = GObject.ParamSpecObject('window', 'window', 'window',
    Pluma.Window, {'READWRITE'})

function QuickOpenPlugin:do_activate()
    ...
end
function QuickOpenPlugin:do_deactivate()
    ...
end
function QuickOpenPlugin:do_update_state()
    ...
end

return {QuickOpenPlugin = QuickOpenPlugin}
```

A lógica de cada plugin (chamadas a Gtk, Gio, GLib, GSettings etc.) é
portável quase 1:1 em nível de API — muda a sintaxe, não a estrutura.

## 3. Estratégia geral de porte

Passos aplicáveis a qualquer um dos plugins Python:

1. Trocar `Loader=python3` por `Loader=lua5.1` no `.plugin.desktop.in.in`.
2. Reescrever os módulos `.py` como módulos `.lua`, mapeando cada classe
   `GObject.Object` (sub)classe para `GObject.Object:derive(...)`.
3. Mapear idiomas de linguagem:
   - `list`/`dict` → `table`;
   - `self.attr` → `self.attr` (LGI também usa `:` para chamadas de método);
   - `try/except` → `pcall`/`xpcall`;
   - `.connect()` de sinais GLib funciona igual em LGI.
4. Manter os arquivos de dados sem alteração — `.ui` (Gtk.Builder),
   `.xml` (snippets, ferramentas), `.gschema.xml` (GSettings), `.tool.in`
   (externaltools). Só o código muda de linguagem.
5. Ajustar `meson.build`/`Makefile.am` do plugin: trocar a instalação de
   arquivos `.py` (hoje via `python.find_installation`) pela instalação dos
   `.lua` em `$prefix/lib/pluma/plugins/<nome>/`.

## 4. Plano por plugin (do mais simples ao mais complexo)

### quickopen (908 LOC, 4 módulos) — primeiro candidato

Lógica autocontida: busca de arquivos, popup Gtk, diretórios virtuais
(`virtualdirs.py`). Poucas dependências externas, boa prova de conceito para
validar o fluxo `Loader=lua5.1` de ponta a ponta antes de investir nos
plugins maiores.

Arquivos a portar: `windowhelper.py`, `popup.py`, `virtualdirs.py`,
`__init__.py`.

### pythonconsole (610 LOC) — excluído do escopo

O propósito deste plugin é expor um **console Python interativo** dentro do
editor (usa o módulo `code` do CPython para implementar um REPL). Não existe
tradução literal para Lua: um "console Lua" seria um plugin com semântica
diferente, não um porte do original.

**Decisão: `pythonconsole` permanece em Python.** Isso é intencional, não uma
lacuna a resolver depois — o plugin continua exigindo o `pythonloader`
existente.

### externaltools (2.734 LOC) — complexidade média-alta

Módulos principais: `manager.py`, `library.py` (gerência de ferramentas),
`capture.py` (execução de processos via `Gio.Subprocess`), `outputpanel.py`,
`filelookup.py`, `linkparsing.py`, `functions.py`. Parsing de `.tool.in`
permanece igual (dado, não código). Complexidade alta por volume de código,
mas a API usada é bem isolada (Gio.Subprocess, Gtk.Builder, GLib).

### snippets (5.680 LOC) — maior e mais arriscado

O maior plugin: parser próprio de substituição (`Parser.py`,
`SubstitutionParser.py`, `Placeholder.py`), integração com
`GtkSource.CompletionProvider` (`Completion.py`), import/export XML
(`Importer.py`, `Exporter.py`), gerência de biblioteca (`Library.py`,
`Manager.py`), drag & drop. Recomendado por último e dividido em sub-etapas:

1. `Parser.py` + `SubstitutionParser.py` (lógica pura, sem UI) — testável
   isoladamente.
2. `Library.py` + `LanguageManager.py` (carregamento dos `.xml` de snippets).
3. `Completion.py` (integração com GtkSourceView).
4. `Manager.py` + `WindowHelper.py` (UI de gerência de snippets).

## 5. Riscos e pontos de atenção

- O loader `lua5.1` do libpeas está preso à Lua 5.1 (via `lua51-lgi`) — não
  há benefício em usar Lua 5.4/5.5 diretamente por esse caminho, mesmo sendo
  as versões mais modernas disponíveis no sistema.
- LGI é bem menos usado e documentado que PyGObject; menos exemplos prontos,
  depuração mais difícil.
- `pythonconsole` não tem tradução real (ver seção 4) e permanece como
  dependência de Python mesmo após o porte dos demais.
- Não há suíte de testes automatizada para os plugins hoje — a validação de
  cada porte será manual.
- Packaging: distros precisarão adicionar `lua51-lgi`/`liblua5.1` como nova
  dependência (a decidir se obrigatória ou opcional, seguindo o padrão do
  Python: `build_bundled_python_loader` em `meson.build` verifica se o loader
  já existe no sistema antes de compilar um embutido).

## 6. Critério de sucesso / validação de cada porte

Para cada plugin portado:

1. Compilar e instalar com `Loader=lua5.1` no `.plugin.desktop.in.in`.
2. Confirmar que o Pluma carrega o plugin sem erros no log (`do_activate`
   chamado).
3. Exercitar o ciclo `do_activate` → `do_update_state` → `do_deactivate`
   (ativar/desativar o plugin pela janela de preferências).
4. Testar manualmente o fluxo principal do plugin (ex.: quickopen — abrir
   arquivo via busca rápida; externaltools — rodar uma ferramenta; snippets —
   expandir um snippet).
