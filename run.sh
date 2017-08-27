#! /bin/sh
filename=$1
nf=$2

filename1=$3
nf1=$4

filename2=$5
nf2=$6

filename3=$7
nf3=$8

if [ $# -lt 2 ]; then
  echo "Usage [module_name number_modules]\n at most 4 modules can be loaded"
  kill $$
fi

path=$(dirname $filename)

i=0
while [ "$nf" -gt $i ]
do
  if sudo rmmod $filename$i.ko 2> /dev/null; then
    echo "Module" $filename$i "was already present. Removed it"
  fi
  i=$(( i+1 ))
done

if [ ! -z $filename1 ]; then
  i1=0
  while [ "$nf1" -gt $i1 ]
  do
    if sudo rmmod $filename1$i1.ko 2> /dev/null; then
      echo "Module" $filename1$i1 "was already present. Removed it"
    fi
    i1=$(( i1+1 ))
  done
fi

if [ ! -z $filename2 ]; then
  i2=0
  while [ "$nf2" -gt $i2 ]
  do
    if sudo rmmod $filename2$i2.ko 2> /dev/null; then
      echo "Module" $filename2$i2 "was already present. Removed it"
    fi
    i2=$(( i2+1 ))
  done
fi

if [ ! -z $filename3 ]; then
  i3=0
  while [ "$nf3" -gt $i3 ]
  do
    if sudo rmmod $filename3$i3.ko 2> /dev/null; then
      echo "Module" $filename3$i3 "was already present. Removed it"
    fi
    i3=$(( i3+1 ))
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

  if [ ! -z $filename2 ]; then
    i2=0
    loaded2=0
    while [ "$nf2" -gt $i2 ]
    do
      if sudo insmod ./$filename2$i2.ko id=$i2; then
        echo "Successfully loaded Module " $filename2$i2
        loaded2=$(( loaded2+1 ))
      else
        echo "Could not load Module " $filename2$i2
        echo "Unloading all other modules"
        j2=0
        while [ "$loaded2" -gt $j2 ]
        do
          if sudo rmmod ./$filename2$j2.ko; then
            echo "Successfully unloaded Module " $filename2$j2
          else
            echo "Error in unloading the module " $filename2$j2
          fi
          j2=$(( j2+1 ))
        done
        break
      fi
      i2=$(( i2+1 ))
    done
  fi

  if [ ! -z $filename3 ]; then
    i3=0
    loaded3=0
    while [ "$nf3" -gt $i3 ]
    do
      if sudo insmod ./$filename3$i3.ko id=$i3; then
        echo "Successfully loaded Module " $filename3$i3
        loaded3=$(( loaded3+1 ))
      else
        echo "Could not load Module " $filename3$i3
        echo "Unloading all other modules"
        j3=0
        while [ "$loaded3" -gt $j3 ]
        do
          if sudo rmmod ./$filename3$j3.ko; then
            echo "Successfully unloaded Module " $filename3$j3
          else
            echo "Error in unloading the module " $filename3$j3
          fi
          j3=$(( j3+1 ))
        done
        break
      fi
      i3=$(( i3+1 ))
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

  if [ ! -z $filename2 ]; then
    i2=0
    while [ "$nf2" -gt $i2 ]
    do
      if sudo rmmod ./$filename2$i2.ko; then
        echo "Successfully unloaded Module " $filename2$i2
      else
        echo "Error in unloading the module $filename2$i2"
      fi
      i2=$(( i2+1 ))
    done
  fi

  if [ ! -z $filename3 ]; then
    i3=0
    while [ "$nf3" -gt $i3 ]
    do
      if sudo rmmod ./$filename3$i3.ko; then
        echo "Successfully unloaded Module " $filename3$i3
      else
        echo "Error in unloading the module $filename3$i3"
      fi
      i3=$(( i3+1 ))
    done
  fi

  # echo "Cleaning..."
  # make clean > /dev/null
  # echo "Cleaned Successfully"
  echo "Terminated"
else
  echo "Could not compile the module"
fi
