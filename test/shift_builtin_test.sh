echo "Before shift:"
echo "\$1=$1 \$2=$2 \$3=$3"

shift

echo "After shift:"
echo "\$1=$1 \$2=$2 \$3=$3"

echo "Remaining arguments: $#"
echo "All args: $*"
