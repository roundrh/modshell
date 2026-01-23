#!/home/rwrlrdrh/Desktop/Modshell_local/msh/msh_prod

set i=0
while [ $i -lt 3 ]; do
  if true; then
    figlet $(($i + 1))
  else
    echo $i
  fi

  sleep 1
  set i=$(($i + 1))
done

figlet kos omak
