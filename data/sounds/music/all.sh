#!/bin/bash
OPUS=1
DELSRC=1
OPUSRATE=64k
MAXRATE=48000
TMPPREF=/tmp/tmp
FILES=(*.wav *.ogg *.flac)
for F in "${FILES[@]}"
do
	if [ $OPUS -eq 1 ]; then
		ffmpeg -y -i "$F" -b:a $OPUSRATE "${F%.*}.opus"
		[ $DELSRC -eq 1 ] && rm "$F"
	else
		CONV=
		if [[ `ffprobe "$F" 2>&1` =~ ([0-9]*)( Hz) ]]; then
			RATE=${BASH_REMATCH%???}
			echo "RATE = $RATE"
			if [[ $RATE -gt $MAXRATE ]]; then
				CONV="-af aresample=resampler=soxr -ar $MAXRATE"
			fi
		fi
		ffmpeg -y -i "$F" -f wav -bitexact $CONV -c:a pcm_s16le "$TMPPREF.wav"
		[ $DELSRC -eq 1 ] && rm "$F"
		mv "$TMPPREF.wav" "$F"
	fi
done

