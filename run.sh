#! /bin/sh
filename=$1
nf=$2

filename1=$3
nf1=$4


if [ $# -lt 2 ]; then
  echo "Usage module_name number_modules module_name number_modules"
  kill $$
fi

path=$(dirname $filename)

i=0
while [ "$nf" -gt $i ]
do
  if sudo rmmod $filename$i.ko 2> /dev/null; then
    echo "Module"  "was already present. Removed it"
  fi
  i=$(( i+1 ))
done

if [ ! -z $filename1 ]; then
  i1=0
  while [ "$nf1" -gt $i1 ]
  do
    if sudo rmmod $filename1$i1.ko 2> /dev/null; then
      echo "Module"  "was already present. Removed it"
    fi
    i1=$(( i1+1 ))
  done
fi


echo "Compiling..."
if cd $path > /dev/null && make > /dev/null && cd -  > /dev/null;then
  echo "Modules Successfully complied"

  i=0
  loaded=0
  while [ "$nf" -gt $i ]
  do
    if sudo insmod ./$filename$i.ko id=$i; then
      echo "Successfully loaded Module " $filename$i
      loaded=$(( loaded+1 ))
    else
      echo "Could not load Module " $filename$i
      echo "Unloading all other modules"
      j=0
      while [ "$loaded" -gt $j ]
      do
        if sudo rmmod ./$filename$j.ko; then
          echo "Successfully unloaded Module " $filename$j
        else
          echo "Error in unloading the module " $filename$j
        fi
        j=$(( j+1 ))
      done
      break
    fi
    i=$(( i+1 ))
  done

  if [ ! -z $filename1 ]; then
    i1=0
    loaded1=0
    while [ "$nf1" -gt $i1 ]
    do
      if sudo insmod ./$filename1$i1.ko id=$i1; then
        echo "Successfully loaded Module " $filename1$i1
        loaded1=$(( loaded1+1 ))
      else
        echo "Could not load Module " $filename1$i1
        echo "Unloading all other modules"
        j1=0
        while [ "$loaded1" -gt $j1 ]
        do
          if sudo rmmod ./$filename1$j1.ko; then
            echo "Successfully unloaded Module " $filename1$j1
          else
            echo "Error in unloading the module " $filename1$j1
          fi
          j1=$(( j1+1 ))
        done
        break
      fi
      i1=$(( i1+1 ))
    done
  fi

  read -rp "Press enter to remove the module or Ctrl+C to exit..." key
  echo "Closing"

  i=0
  while [ "$nf" -gt $i ]
  do
    if sudo rmmod ./$filename$i.ko; then
      echo "Successfully unloaded Module " $filename$i
    else
      echo "Error in unloading the module $filename$i"
    fi
    i=$(( i+1 ))
  done

  if [ ! -z $filename1 ]; then
    i1=0
    while [ "$nf1" -gt $i1 ]
    do
      if sudo rmmod ./$filename1$i1.ko; then
        echo "Successfully unloaded Module " $filename1$i1
      else
        echo "Error in unloading the module $filename1$i1"
      fi
      i1=$(( i1+1 ))
    done
  fi

  # echo "Cleaning..."
  # make clean > /dev/null
  # echo "Cleaned Successfully"
  echo "Terminated"
else
  echo "Could not compile the module"
fi
