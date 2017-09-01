#! /bin/bash
filename0=$1
nf0=$2

filename1=$3
nf1=$4

filename2=$5
nf2=$6

if [ $(( $# % 2)) -ne 0 ]; then
  echo "Missing the number_modules" ;
  echo "Usage: module_name number_modules" ;
  kill $$
fi

if [ $# -lt 2 ]; then
  echo "At least one module must be loaded" ;
  kill $$
fi

elements=$( expr $# / 2 )
path=$(dirname $filename0)

tmp=0
while [ "$elements" -gt $tmp ]
do
  name=filename$tmp
  number=nf$tmp
  if [ ! -z ${!name} ]; then
    i=0
    while [ "${!number}" -gt $i ]
    do
      if sudo rmmod ${!name}$i.ko 2> /dev/null; then
        echo "Module" ${!name}$i "was already present. Removed it"
      fi
      i=$(( i+1 ))
    done
  fi
  tmp=$(( tmp+1 ))
done

echo "Compiling..."
if cd $path > /dev/null && make > /dev/null && cd -  > /dev/null;then
  echo "Modules Successfully complied"

  tmp=0
  while [ "$elements" -gt $tmp ]
  do
    name=filename$tmp
    number=nf$tmp
    if [ ! -z ${!name} ]; then
      i=0
      loaded=0
      while [ "${!number}" -gt $i ]
      do
        if sudo insmod ./${!name}$i.ko id=$i; then
          echo "Successfully loaded Module " ${!name}$i
          loaded=$(( loaded+1 ))
        else
          echo "Could not load Module " ${!name}$i
          echo "Unloading all other modules"
          j=0
          while [ "$loaded" -gt $j ]
          do
            if sudo rmmod ./${!name}$j.ko; then
              echo "Successfully unloaded Module " ${!name}$j
            else
              echo "Error in unloading the module " ${!name}$j
            fi
            j=$(( j+1 ))
          done
          kill $$
        fi
        i=$(( i+1 ))
      done
    fi
    tmp=$(( tmp+1 ))
  done

  read -rp "Press enter to remove the module or Ctrl+C to exit..." key
  echo "Closing"

  tmp=0
  while [ "$elements" -gt $tmp ]
  do
    name=filename$tmp
    number=nf$tmp
    if [ ! -z ${!name} ]; then
      i=0
      while [ "${!number}" -gt $i ]
      do
        if sudo rmmod ./${!name}$i.ko; then
          echo "Successfully unloaded Module " ${!name}$i
        else
          echo "Error in unloading the module ${!name}$i"
        fi
        i=$(( i+1 ))
      done
    fi
    tmp=$(( tmp+1 ))
  done

# echo "Cleaning..."
# make clean > /dev/null
# echo "Cleaned Successfully"
  echo "Terminated"
else
  echo "Could not compile the module"
fi
