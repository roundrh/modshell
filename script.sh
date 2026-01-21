#!/home/rwrlrdrh/Desktop/Modshell_local/msh/msh

set i=0
while true; do
  if (ps aux | grep 'firefox' | grep -v grep); then
    pkill firefox
  else
    sleep 5
    echo "process not found... sleeping."
  fi
done

figlet done script
