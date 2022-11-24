#!/bin/bash

##### PWD #####
curr_dir=$PWD
dir=${curr_dir##*/}

if [ $dir = 'daemons' -o $dir = 'srcs' -o $dir = 'config' -o $dir = 'scripts' ]; then
	cd ..
elif [ $dir = 'taskmaster' ]; then
	cd ./test
elif [ $dir = 'src' -o $dir = 'include' ]; then
	cd ../test
elif [ $dir = 'test' ]; then
	cd .
else
	echo "you are'nt in a good directory, go to taskmaster/";
	exit;
fi

BIN="./daemons/"
daemons=($BIN*)

if [ "$(ls -A $BIN)" == "" ]; then
	echo "empty directory";
	exit;
fi

shut_this_shit_down() {
	for str in "$@"; do
		name=${str##*/}
		pid=$(pgrep $name);
		if [ "$pid" ]; then
			echo "kill $name, pid $pid";
			kill -9 $pid;
		fi
	done
}

shut_this_shit_down ${daemons[@]};
