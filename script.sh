#!/home/rwrlrdrh/Desktop/Modshell_local/msh/msh_prod

set i=0
while [ $i -lt 4 ]; do
  if ([ $i -lt 3 ]); then
    figlet $(($i + 1))
  else
    echo $(($i + 1))
  fi

  sleep 1
  set i=$(($i + 1))
done

figlet 'palestine > jordan'
