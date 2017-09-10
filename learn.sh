#! /bin/bash
norm=$1
trim=$2
cantrim=$3

if [ $# -lt 1 ]; then
  echo "Please only insert the number of learner you want to load
(optionally) followed by the index of the trim learner and after
how many iid it should trim
Example:
./learn.sh 2 1
Load 2 klearner (klearner0.ko, klearner1.ko) and klearner1 is a trim learner" ;
  kill $$
fi

if [ $# -eq 1 ]; then
  trim=-1
  echo "No trim"
fi

if [ $# -eq 2 ]; then
  cantrim=100000
  echo "Cantrim is default to 100 000"
fi

if [ $trim -ge $norm ]; then
  echo "With "$norm" modules, trim index must between 0 and " $(( norm-1 ))
  kill $$
fi

path=$(dirname klearner)

i=0
while [ "$norm" -gt $i ]
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
  while [ "$norm" -gt $i ]
  do
    if [ "$i" -eq $trim ]; then
      echo "####Trim module #####"
      trim=$cantrim
    else
      trim=0
    fi

    if sudo insmod ./klearner$i.ko cantrim=$trim id=$i; then
      echo "Successfully loaded  Module " klearner$i
      loaded=$(( loaded+1 ))
      if [ "$i" -eq $trim ]; then
        echo "####################"
      fi
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
  while [ "$norm" -gt $i ]
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
