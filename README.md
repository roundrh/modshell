# modshell (msh)
A semi-minimal POSIX-lite shell written in C.

Made to try implementing a concept where a shell contains quality-of-life interactive features similar to those found in oh-my-zsh, while keeping a size around the size of dash, and optimized code to be within some percentage of dash's general performance in script execution. The shell does this by focusing on POSIX features while avoiding non-POSIX features and remaining minimal where it will add unneeded overhead.

This project is still incomplete, TODO is below.

## Build

Clone the repository
```
git clone https://github.com/roundrh/modshell/
cd modshell
```

Run make with desired build, prod recommended for general use. (list below)
```
make prod
```

Run the shell
```
./msh_prod
```

The 3 builds are: all, dev, prod.
- **all**: Default when running make, contains ASan flags to detect memory leaks, invalid reads, etc. `./msh`
- **dev**: Contains no extra flags, mainly used for valgrind and debugging in gdb. `./msh_dev`
- **prod**: -O3 optimized build, this is the build for real usage, or to run scripts. `./msh_prod`

## Requirements 
- GCC
- make

## Key Features
- Zero dependency TUI, built with raw mode termios and ANSI escape codes
- Autosuggestions:
  - Renders ghost text, right arrow to autocomplete 
suggestion
  - Disable/Enable in shell via shopt +as or shopt -as
  - Disable/Enable persistent via export MSH_RENDER_AUTOSGST=0 or export MSH_RENDER_AUTOSGST=1 found in ~/.mshrc
- Interactive tab completion:
  - Single tab lists possible matches
  - Double tab launches interactive loop, scrollable with tab, or arrow keys, enter to append, any key to quit.
  - Includes written pager to scroll large lists of tab completions.
- Custom Prompts:
  - Using PS1/PS2 via export PS1 or export PS2 to allow for custom prompts and shell expansions
  - These variables follow dynamic expansion, meaning export PS1='$PWD' or export PS1='$(git_branch)' will expand PWD everytime the shell updates the prompt after a command, i.e. upon reading input again
  - Escape characters are parsed in the same way \n will append a newline and such
  - ~/.mshrc shows a default config for PS1 to demonstrate how it expands
- Arena Allocator for command life-cycles
- Flow Control:
  - if/elif/else
  - for
  - while
  - until
- POSIX Parameter Expansion:
  - Braces: `${#VAR}`, `${VAR#, ##, %, %%}`, `${VAR:-, :+, :=, :?`, `${VAR}`
  - Variable: `$VAR`
  - Subshell: `$(command)` (backtick syntax not supported)
  - Arithmetic: `$((...))`
  - Positional Parameters `$#`, `$*`, `$@`, `$n`
  -  Basic Expansions: `$$`, `$?`
- Brace ranges and options expansions:
  - `{a..z}`, `{1..n}`, `prefix{a,{b,c}}suffix`
- IFS Splitting, with variable IFS options via export IFS or IFS='' parsing
- Full Job Control: `fg`, `bg`, `jobs`
- AST recursive descent parser
- Supports all defined POSIX command types, except legacy backticks and brace command grouping outside of loops for now
- Hashtables for aliases, builtins, environment entries, and PATH caching
- Heredoc with expansions and leading tab removal, including pipe heredoc, script heredocs, etc.
- All / Arbitrary redirections (- to close fd not handled yet)
- Aliases
- Functions
- msh -c "command"
- Terminal state capture for stty/reset/... commands
## License
MIT
