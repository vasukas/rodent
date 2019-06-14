#!/bin/bash

function show_help {
	echo "Usage: vb.sh <TARGET> <DBG_LEVEL> [OPTIONS]"
	echo "DBG_LEVEL is one of:"
	echo "  0 debug (with symbols)"
	echo "  1 release (with symbols)"
	echo "  2 optimized & minimized release"
	echo "Options:"
	echo "  clean  performs complete rebuild"
    echo "  p<N>   set number of compilation processes"
}

if [ "$#" -lt 2 ]; then
	show_help
	exit 1
fi

Target=$1
Dbglev=$2
DoClean=0
ProcCount=0

ArgCount=0
for Arg in "$@"; do
	if [ $ArgCount -lt 2 ]; then
		((ArgCount++))
	elif [ "$Arg" == "clean" ]; then
		DoClean=1
	elif [[ "$3" =~ p^[0-9]+$ ]]; then
		ProcCount=${Arg:1}
	else
		echo "Invalid argument: $Arg"
		show_help
		exit 1
	fi
done

if [ $ProcCount -eq 0 ]; then
	ProcCount=$(grep -c ^processor /proc/cpuinfo)
fi

WarnFlags=(
	-Wall
	-Winit-self
	-Wnon-virtual-dtor
	-Wunreachable-code
	-Wno-attributes
)
FlagsDbg=(
	-g
)
FlagsRel=(
	-g
	-O2
)
FlagsOpt=(
	-s
	-O2
	-flto
)

ObjDirPrefix=../build/vb_obj
BinDirPrefix=../build/vb_bin



function info {
	echo -n "$(tput setab 15)$(tput setaf 0)"
	echo -n $@
	echo "$(tput sgr0)"
}

if ! [ -f vb_project ]; then
	info "Project variables file does not exist"
	exit 1
fi
source vb_project

if ! [ -f vb_tar_"$Target" ]; then
	info "Target variables file does not exist for \"$Target\""
	exit 1
fi
source vb_tar_"$Target"

if   [ $Dbglev == 0 ]; then
	info "Debug build"
	   Flags+=( ${FlagsDbg[@]} )
	LnkFlags+=( ${FlagsDbg[@]} )
	Target=${Target}_debug
elif [ $Dbglev == 1 ]; then
	info "Fast release build"
	   Flags+=( ${FlagsRel[@]} )
	LnkFlags+=( ${FlagsRel[@]} )
	Target=${Target}_rel
elif [ $Dbglev == 2 ]; then
	info "Optimized build"
	   Flags+=( ${FlagsOpt[@]} )
	LnkFlags+=( ${FlagsOpt[@]} )
	Target=${Target}_opt
else
	echo "Wrong Dbglev"
	exit 1
fi

[ -f vb_project_post ] && source vb_project_post

CmpExe=$Compiler
CmpAr=$CompilerLnk
ObjDir="$ObjDirPrefix"/$Target

info "Building \"${ProName}\" for $Target"



[ -d "$ObjDir" ] || mkdir -p "$ObjDir" || exit 1

for (( i=0; i<${#Source[@]}; i++ )) {
	Fname=$(basename "${Source[i]}")
	Objects[i]="$ObjDir/$Fname".o
}



InfoFile="$ObjDir/_flags.txt"
ReLink=0

function clean_info {
	echo "PrevFlags=( ${Flags[@]} )" > "$InfoFile"
	echo "PrevLnkFlags=( ${LnkFlags[@]} )" >> "$InfoFile"
	ReLink=1
}
function clean {
	for (( i=0; i<${#Objects[@]}; i++ )) {
		rm -f "${Objects[i]}"
	}
	clean_info
}

if [ $DoClean -eq 1 ]; then
	info "Forced total rebuild"
	clean
elif [ ! -f "$InfoFile" ]; then
	info "Build info not found - total rebuild"
	clean
else
	source "$InfoFile"
	if [ "${PrevFlags[*]}" != "${Flags[*]}" ] ; then
		info "Build flags changed - total rebuild"
		clean
	elif [ "${PrevLnkFlags[*]}" != "${LnkFlags[*]}" ] ; then
		info "Link flags changed - re-linking"
		clean_info
	else
		info "Build info not changed"
	fi
fi



info "Using $ProcCount processes"

PrCou=0
PrPids=()
PrExit=0

# https://unix.stackexchange.com/a/436932
function pr_wait {
	for job in "${PrPids[@]}"; do
		Code=0
		wait ${job} || Code=$?
		if [ $Code != 0 ]; then
			PrExit=1
		fi
	done 
	PrCou=0
}

info "Checking sources"
Flags+=( ${WarnFlags[@]} )

for (( i=0; i<${#Source[@]}; i++ )) {
	if [ ! -f "${Source[i]}" ]; then
		info "Source file doesnt exist: ${Source[i]}"
		exit 1
	fi
	if [ $PrCou -eq $ProcCount ]; then
		pr_wait
		if [ $PrExit != 0 ]; then
			info "Compilation failed"
			exit 1
		fi
	fi
	if [ "${Source[i]}" -nt "${Objects[i]}" ] || [ ! -f "${Objects[i]}" ]; then
		(echo $CmpExe ${Flags[@]} -c "${Source[i]}" -o "${Objects[i]}" | bash) &
		PrPids+=("$!")
		((PrCou++))
		ReLink=1
	fi
}

pr_wait
if [ $PrExit != 0 ]; then
	info "Compilation failed"
	exit 1
fi



if [ $IsLibrary -eq "0" ]; then
	mkdir -p "$BinDirPrefix"
	Output="$BinDirPrefix"/${ProName}

	if [ "$Platform" == "win" ]; then
		Output=$Output.exe
	fi

	if [ $ReLink -eq 1 ] || [ ! -f $Output ]; then
		info "Link flags: ${LnkFlags[@]}"

		info "Linking..."
		$CmpExe ${Flags[@]} ${Objects[@]} -o $Output ${LnkFlags[@]}
		if [ $? != 0 ]; then
			info "Link fail"
			exit 1
		else
			info "OK finished"
		fi
	else
		info "No changes"
		exit 2
	fi
else
	if [ "$Platform" == "win" ]; then
		Output=$Winlibs/lib/lib$ProName.a
	else
		Output=lib$ProName.a
	fi

	info "Building..."
	$CmpAr rcs $Output ${Objects[@]}
	if [ $? != 0 ]; then
		info "Link fail"
		exit 1
	else
		info "OK finished"
	fi
fi
