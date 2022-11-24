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

build_daemon() {
	if [ "$1" ]; then
		export DAEMON_NAME=$1;
	fi
	# if [ "$2" ]; then
	# 	export SLEEP_TIME=$2;
	# fi
	make -s -C ./srcs
}

build_daemon "ALPHA"
build_daemon "BETA"
build_daemon "GAMMA"
build_daemon "DELTA"
build_daemon "EPSILON"
