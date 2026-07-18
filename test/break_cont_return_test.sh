#!/bin/sh

echo "1. Nested loop + continue/break test"

for i in 1 2 3; do
  echo "outer i=$i"
  for j in 1 2 3; do
    if [ "$j" -eq 2 ]; then
      echo "  continue inner at j=$j"
      continue
    fi
    if [ "$j" -eq 3 ] && [ "$i" -eq 2 ]; then
      echo "  break inner at i=$i j=$j"
      break
    fi
    echo "  inner i=$i j=$j"
  done
done

echo ""
echo "2. return propagation test"

test_return() {
  echo "inside function"
  for i in 1 2 3; do
    echo "  i=$i"
    if [ "$i" -eq 2 ]; then
      echo "  returning from function"
      return 7
    fi
  done
  echo "this should NOT print if return works"
}

test_return
echo "after function call, status=$?: should be 7"

echo ""
echo "3. continue + while loop edge case"

i=0
while [ "$i" -lt 5 ]; do
  i=$((i + 1))
  if [ "$i" -eq 3 ]; then
    echo "continue at i=$i"
    continue
  fi
  echo "i=$i"
done

echo "DONE"
