
BUGS:
kill -9 %$(echo 1):
%$ standalone string (echo 1) detected at wrong time in make_string [FIXED]
double backspace to erase last suggestion on empty text


IMPLEMENT:
[X] Nested Param Expansion: only recurse for ${} and $(()) - no other expansion I have right now requires nested
[] Flow Control: for, if, while (before the rest)
[] Tab Completion: ioctl for terminal height + width to fix most bugs left with userinp.c - then zsh style tab completion with scrolling ability.
[] - Prompt Customization (PS1, turning autosugesstions on and off, changing what kind of cursor is used, etc)
[] - ensure UTF-8 

