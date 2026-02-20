# modshell (msh)
A semi-minimal POSIX-lite shell written in C.

Developed over a few months as a side-project to learn systems programming and as a first project.
Focuses on educational depth with a simple and readable codebase to make
complex features (Arenas, TUI implementations (without any external libraries), job control, ASTs, parameter expansions) seem more intuitive.

This project is still a work in progress.

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

## Key Features (With Technical Details) 
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
  - Subshell: `$(command)`
  - Arithmetic: `$((...))`
  - Positional Parameters `$#`, `$*`, `$@`, `$n`
  -  Basic Expansions: `$$`, `$?`
- Bash brace ranges and options expansions:
  - `{a..z}`, `{1..n}`, `prefix{a,{b,c}}suffix`
- IFS Splitting, with variable IFS options
- Full Job Control: `fg`, `bg`, `jobs`
- AST Parser to allow for complex commands
- Hashtables for aliases, builtins, environment entries, and PATH caching
- Heredoc (Doesnt expand yet)
- Basic redirections `<`, `>`, `>>`
- Aliases
## License
MIT
