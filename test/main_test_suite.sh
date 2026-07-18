pass=0
fail=0

run() {
  local cmd="$1"
  local expected="$2"

  eval "$cmd" >tmpfile 2>&1
  output="$(cat tmpfile)"
  if [ "$output" = "$expected" ]; then
    echo "PASS: $cmd"
    pass=$((pass + 1))
    return 0
  else
    echo "FAIL: $cmd"
    echo "Expected: $expected"
    echo "Got: $output"
    fail=$((fail + 1))
    return 1
  fi
}

run ':' ''
run 'true' ''
run 'false' ''
run 'echo hello' 'hello'
run 'printf hello' 'hello'
run 'printf %s hello' 'hello'
run 'printf %s ""' ''
run 'echo 123' '123'
run 'echo abc def' 'abc def'
run 'x=1; echo $x' '1'
run 'x=hello; echo $x' 'hello'
run 'x=1; y=2; echo $x$y' '12'
run 'x=1; y=2; echo "$x $y"' '1 2'
run 'unset x; echo ${x:-foo}' 'foo'
run 'x=bar; echo ${x:-foo}' 'bar'
run 'unset x; x=abc; echo $x' 'abc'
run 'x=abc; echo $x' 'abc'
run 'unset x; export x=abc; echo $x' 'abc'
run 'echo $(echo hello)' 'hello'
run 'echo $(printf abc)' 'abc'
run 'x=$(printf hello); echo $x' 'hello'
run 'echo $((1+1))' '2'
run 'echo $((2*3))' '6'
run 'echo $((8/2))' '4'
run 'echo $((7-5))' '2'
run 'echo $((5%2))' '1'
run 'echo $(((2+3)*4))' '20'
run 'echo $((+5))' '5'
run 'echo $((-5))' '-5'
run 'echo "$((10))"' '10'
run 'echo "a"' 'a'
run 'echo "a b"' 'a b'
run 'echo '\''a'\''' 'a'
run 'echo ""' ''
run 'x="a b"; echo "$x"' 'a b'
run 'x=""; echo "$x"' ''
run 'true && echo ok' 'ok'
run 'false || echo ok' 'ok'
run 'true && true && echo yes' 'yes'
run 'false || false || echo yes' 'yes'
run 'true || echo bad' ''
run 'false && echo bad' ''
run '(echo hello)' 'hello'
run '(x=1; echo $x)' '1'
run 'x=1; (x=2); echo $x' '1'
run '( echo hello; )' 'hello'
run 'f() { echo hello; }; f' 'hello'
run 'f() { echo "$1"; }; f abc' 'abc'
run 'f() { echo "$#"; }; f a b c' '3'
run 'f() { echo "$*"; }; f a b c' 'a b c'
run 'f() { echo "$@"; }; f a b' 'a b'
run 'f() { return; }; f' ''
run 'for i in 1; do echo $i; done' '1'
run 'if true; then echo yes; fi' 'yes'
run 'if false; then echo no; else echo yes; fi' 'yes'
run 'if false; then echo no; elif true; then echo yes; fi' 'yes'
run 'echo abc | cat' 'abc'
run 'printf abc | cat' 'abc'
run 'echo hello | cat | cat' 'hello'
run 'echo one >f; cat <f' 'one'
run 'eval "echo hello"' 'hello'
run 'eval "x=5; echo \$x"' '5'
run 'x=1; export x; echo $x' '1'
run 'unset x; echo $x' ''
run 'x=abcdef; echo ${x#abc}' 'def'
run 'x=abcdef; echo ${x%def}' 'abc'
run 'x=abcabc; echo ${x##*c}' ''
run 'x=abcabc; echo ${x%%c*}' 'ab'
run 'shift 0; echo ok' 'ok'
run 'times >/dev/null; echo ok' 'ok'
run '. ./nonexistent 2>/dev/null || echo ok' 'ok'
run 'unset IFS; printf "%s" "a b"' 'a b'
run 'echo $(echo $(echo nested))' 'nested'
run 'x=$(printf a); y=$(printf b); echo $x$y' 'ab'
run 'echo $((2+2*3))' '8'
run 'echo $(((2+2)*3))' '12'
run 'echo done' 'done'
run 'x=abc; echo ${x}' 'abc'
run 'unset x; echo ${x:+foo}' ''
run 'x=abc; echo ${x:+foo}' 'foo'
run 'unset x; echo ${x:=foo}; echo $x' 'foo
foo'
run 'x=abc; echo ${#x}' '3'
run 'true; echo $?' '0'
run 'false; echo $?' '1'
run 'false; true; echo $?' '0'
run 'false || true; echo $?' '0'
run 'true && false; echo $?' '1'
run 'echo "a$b"' 'a'
run 'b=hello; echo "a$b"' 'ahello'
run "echo 'a\$b'" 'a$b'
run "echo 'a\"b'" 'a"b'
run 'echo "" "" | wc -w' '0'
run 'x="a b"; printf "<%s>" $x' '<a><b>'
run 'x="a b"; printf "<%s>" "$x"' '<a b>'
run 'echo "$(echo hello)"' 'hello'
run 'x=$(echo hi); echo "$x"' 'hi'
run 'printf abc | tr a z' 'zbc'
run 'echo hello | grep h' 'hello'
run 'echo hello | grep z' ''
run 'echo hi >f; cat f; rm f' 'hi'
run 'printf abc >f; cat <f; rm f' 'abc'
run 'echo hi >>f; echo there >>f; cat f; rm f' 'hi
there'
run 'echo out; echo err >&2' 'out'
run 'echo err >&2 2>/dev/null; echo ok' 'ok'
run 'f(){ echo hi; }; f; f' 'hi
hi'
run 'x=1; f(){ x=2; }; f; echo $x' '2'
run 'f(){ return 7; }; f; echo $?' '7'
run 'for i in a b c; do echo $i; done' 'a
b
c'
run 'i=1; while [ $i -le 3 ]; do echo $i; i=$((i+1)); done' '1
2
3'
run 'i=1; until [ $i -gt 2 ]; do echo $i; i=$((i+1)); done' '1
2'
run '[ 1 -eq 1 ] && echo yes' 'yes'
run '[ 1 -ne 2 ] && echo yes' 'yes'
run '[ abc = abc ] && echo yes' 'yes'
run 'true && echo a && echo b' 'a
b'
run 'false || echo a || echo b' 'a'
run 'true && false || echo yes' 'yes'
run 'x=1; (x=2); echo $x' '1'
run '(echo one; echo two)' 'one
two'
run 'readonly x=1; echo $x' '1'

echo "PASS: $pass"
echo "FAIL: $fail"

rm tmpfile
