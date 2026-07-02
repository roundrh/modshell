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
  - Renders ghost text, right arrow to autocomplete suggestion
- Interactive tab completion:
  - Single tab lists possible matches
  - Double tab launches interactive loop, scrollable with tab, or arrow keys, enter to append, any key to quit.
  - Includes written pager to scroll large lists of tab completions.
- Arena Allocator for command life-cycles
- Flow Control:
  - if/elif/else
  - for
  - while
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
- Supports all defined posix command types
- Hashtables for aliases, builtins, environment entries, and PATH caching
- Heredoc with expansions and leading tab removal, including pipe heredoc, script heredocs, etc.
- All / Arbitrary redirections (- to close fd not handled yet)
- Aliases
- Functions
- msh -c "command"
- Terminal state capture for stty/reset/... commands
## TODO
Ranked by priority:
  - Add switch/case
  - Fix inconsistent error paths (exit calls on ENOMEM must be an error return not exit)
  - Change strdup calls in get_matches / tab_dbl pager pipeline to use arena (userinp.c)
  - Finish set builtin
  - Add PS1 and completion for multi-line input
  - Add multi-line prompts
## License
MIT
