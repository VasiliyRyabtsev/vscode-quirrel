# vscode-quirrel

Quirrel language support extension.
Based on https://github.com/mepsoid/vscode-s-quirrel

Features:

* Syntax highlighting
* Syntax check and static anaysis
* Code navigation (Go To Declaration)

## Setup

* Run VS Code;
* Open extension marketplace panel, find and install `Quirrel` language support extension;
* Open settings window (Ctrl+<,>) and find Extensions/Quirrel section;
* Setup path to the squirrel code runner `Quirrel/Code Runner/File Name` executable, with default value `csq-dev`. Note: you can also use name of the environmental variable, full path or short file name;
* You are ready to go;

## Some hints on usage

* Start typing identifier name finishing with `=` symbol. If this key is known tou will get list of appropriate substitution values;
* Set cursor to `require()` method argument with module path and press `F12`. If module found it will be opened in new tab;
* Type `require("` with some known characters of module file name and press Ctrl+<Space> to get list of paths to suitable files from workspace;
* Save document to run code checker. All found code errors will be displayed at common Problems panel;
* Create or open document from disk and press Ctrl+<F5> to run it. Found compile time or run time errors will be displayed at Problems pabel. All output will be displayed and highlighted at Output panel;

## References

### Language and syntax highlighting

- [Gaijin's Quirrel language reference](http://quirrel.io/doc/reference/diff_from_original.html)
- [VSCode highlight guide](https://code.visualstudio.com/api/language-extensions/syntax-highlight-guide)
- [Sublime Text syntax definitions](https://sublime-text-unofficial-documentation.readthedocs.io/en/latest/reference/syntaxdefs.html)
- [Oniguruma regexp syntax](https://macromates.com/manual/en/regular_expressions)
- [TextMate language grammar](https://macromates.com/manual/en/language_grammars)
- [Standard scope naming](https://www.sublimetext.com/docs/3/scope_naming.html)

### Repositories

- [Quirrel language](https://github.com/GaijinEntertainment/quirrel)
- [Microsoft JS highlighting rules](https://github.com/microsoft/vscode/blob/master/extensions/javascript/syntaxes/JavaScript.tmLanguage.json)
- [Beautifier for javascript](https://github.com/beautify-web/js-beautify/)

