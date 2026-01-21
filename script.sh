#!/home/rwrlrdrh/Desktop/Modshell_local/msh/msh

set i=0
while [ $i -lt 10 ]; do
  if ([ $i -lt 6 ]); then
    figlet $i
    sleep 1
    set i=$(($i + 1))
  else
    echo $i
    sleep 1
    set i=$(($i + 1))
  fi
done

figlet done script
