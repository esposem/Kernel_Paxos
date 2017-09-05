#! /bin/bash
trim=$1
norm=$2
tot=$(( trim + norm ))

if [ $# -lt 2 ]; then
  echo "Please only insert the number of trim learner you want to load
followed by the number of normal learner
Example:
./learn.sh 3 2
Load 3 trim klearner (klearner0.ko, klearner1.ko, klearner2.ko) and 2 normal klearner (klearner3.ko, klearner4.ko) " ;
  kill $$
fi

path=$(dirname klearner)

i=0
while [ "$tot" -gt $i ]
do
  if sudo rmmod klearner$i.ko 2> /dev/null; then
    echo "Module" klearner$i "was already present. Removed it"
  fi
  i=$(( i+1 ))
done



echo "Compiling..."
if cd $path > /dev/null && make > /dev/null && cd -  > /dev/null;then
  echo "Modules Successfully complied"


  i=0
  loaded=0
  cantrim=0
  while [ "$tot" -gt $i ]
  do
    if [ "$i" -lt $trim ]; then
      cantrim=100000
    else
      cantrim=0
    fi

    if sudo insmod ./klearner$i.ko cantrim=$cantrim id=$i; then
      echo "Successfully loaded  Module " klearner$i
      loaded=$(( loaded+1 ))
    else
      echo "Could not load Module " klearner$i
      echo "Unloading all other modules"
      j=0
      while [ "$loaded" -gt $j ]
      do
        if sudo rmmod ./klearner$j.ko; then
          echo "Successfully unloaded Module " klearner$j
        else
          echo "Error in unloading the module " klearner$j
        fi
        j=$(( j+1 ))
      done
      kill $$
    fi

    i=$(( i+1 ))
  done


  read -rp "Press enter to remove the module or Ctrl+C to exit..." key
  echo "Closing"


  i=0
  while [ "$tot" -gt $i ]
  do
    if sudo rmmod ./klearner$i.ko; then
      echo "Successfully unloaded Module " klearner$i
    else
      echo "Error in unloading the module klearner$i"
    fi
    i=$(( i+1 ))
  done



# echo "Cleaning..."
# make clean > /dev/null
# echo "Cleaned Successfully"
  echo "Terminated"
else
  echo "Could not compile the module"
fi
