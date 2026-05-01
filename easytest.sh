#!/bin/bash
abort_action=0

OUTPUT_FREQ=434.0
RESOURCES_LOCATION="${RPITX_RESOURCES_LOCATION:-/usr/share/rpitx-ui}"
DEFAULT_POCSAG_MESSAGE="1:YOURCALL\n2: Hello world"
DEFAULT_OPERA_CALLSIGN="F5OEO"
DEFAULT_RTTY_MESSAGE="HELLO WORLD FROM RPITX"
DEFAULT_CW_MESSAGE="CQ CQ DE RPITX"
DEFAULT_CW_WPM=5
DEFAULT_RFGEN_SAMPLE_RATE=500000
DEFAULT_RFGEN_BANDWIDTH=200000
DEFAULT_MULTITONE_TONES=8
DEFAULT_NFM_MODE="Wide"
DEFAULT_PLAYBACK="loop"
DEFAULT_SSB_SIDEBAND="USB"
DEFAULT_RDS_PI="0x1234"
DEFAULT_RDS_PS="rpitx-ui"
DEFAULT_RDS_RT="rpitx-ui Broadcast WFM with RDS"
DEFAULT_RDS_PE="50"
LAST_ITEM="0 Tune"
AUDIO_FILE_PATTERN='\.(aif|aiff|caf|flac|mp3|wav)$'

do_check_file_existance() 
{

	if ! readlink -e "$1" > /dev/null; then
    	whiptail --title "Error!" --msgbox "The file does not exist!" 8 78
		return 1
	fi
	return 0

}

do_freq_setup()
{

if FREQ=$(whiptail --inputbox "Enter output frequency (in MHz). Current is $OUTPUT_FREQ MHz" 8 78 $OUTPUT_FREQ --title "rpitx-ui transmit frequency" 3>&1 1>&2 2>&3); then
	OUTPUT_FREQ=$FREQ
fi

}

do_file_choose() {
	local file_type_info="$1"
	local directory="$2"
	local file_pattern="${3,,}"
	local path file displayed_info selected_file
	local file_list=()

	for path in "$directory"/*; do
		[[ -f "$path" ]] || continue

		file=${path##*/}
		if [[ "${file,,}" =~ $file_pattern ]]; then
			file_list+=("$file" "")
		fi
	done

	if (( ${#file_list[@]} == 0 )); then
		whiptail --title "No Files Found" --msgbox "No $file_type_info files were found in $directory" 8 78
		abort_action=1
		return
	fi

	displayed_info="Choose $file_type_info file \nlocated in $directory:"
	if selected_file=$(whiptail --noitem --title "Select a file to transmit" --menu "$displayed_info" 21 82 12 "${file_list[@]}" 3>&1 1>&2 2>&3); then
		FILE_LOC="${directory%/}/$selected_file"
		abort_action=0
	else
		abort_action=1
	fi
}

do_enter_message()
{

LAST_ITEM="$menuchoice"
if MESSAGE=$(whiptail --inputbox "Type custom $1 message:" 8 78 "$2" --title "Enter message to transmit" 3>&1 1>&2 2>&3);  then
	abort_action=0
	if [ -z "$MESSAGE" ]; then
    	whiptail --title "Error!" --msgbox "Empty message!" 8 78
		abort_action=1
	fi
else
	abort_action=1
fi

}

do_enter_wpm()
{

if CW_WPM=$(whiptail --inputbox "Enter CW speed (WPM):" 8 78 $DEFAULT_CW_WPM --title "CW speed" 3>&1 1>&2 2>&3); then
	abort_action=0
	if [ -z "$CW_WPM" ]; then
		whiptail --title "Error!" --msgbox "Empty WPM value!" 8 78
		abort_action=1
	elif ! [[ "$CW_WPM" =~ ^[0-9]+$ ]]; then
		whiptail --title "Error!" --msgbox "WPM must be a positive integer!" 8 78
		abort_action=1
	fi
else
	abort_action=1
fi

}

do_enter_rfgen_params()
{

LAST_ITEM="$menuchoice"

# Mode selection
if RFGEN_MODE=$(whiptail --title "RF generator mode" --menu "Select RF generator mode:" 15 78 3 \
	"Noise" "Uniform pseudo-random noise across the bandwidth" \
	"Sweep" "Fast sawtooth sweep across the bandwidth" \
	"Multitone" "Random fast-hopping across equidistant tones" \
	3>&1 1>&2 2>&3); then
	abort_action=0
else
	abort_action=1
	return
fi

# Bandwidth
if RFGEN_BW=$(whiptail --inputbox "Enter RF generator bandwidth (Hz, must be below $DEFAULT_RFGEN_SAMPLE_RATE):" 8 78 "$DEFAULT_RFGEN_BANDWIDTH" --title "RF generator bandwidth" 3>&1 1>&2 2>&3); then
	if [ -z "$RFGEN_BW" ] || ! [[ "$RFGEN_BW" =~ ^[0-9]+$ ]] || [ "$RFGEN_BW" = "0" ] || [ "$RFGEN_BW" -ge "$DEFAULT_RFGEN_SAMPLE_RATE" ]; then
		whiptail --title "Error!" --msgbox "Bandwidth must be a positive integer below $DEFAULT_RFGEN_SAMPLE_RATE Hz!" 8 78
		abort_action=1
		return
	fi
else
	abort_action=1
	return
fi

# Tone count is asked only for multitone mode; left empty for other modes.
MULTITONE_TONES=""
if [ "$RFGEN_MODE" = "Multitone" ]; then
	if MULTITONE_TONES=$(whiptail --inputbox "Enter tone count:" 8 78 "$DEFAULT_MULTITONE_TONES" --title "Multitone tone count" 3>&1 1>&2 2>&3); then
		if [ -z "$MULTITONE_TONES" ] || ! [[ "$MULTITONE_TONES" =~ ^[0-9]+$ ]] || [ "$MULTITONE_TONES" -lt 2 ] || [ "$MULTITONE_TONES" -gt 1024 ]; then
			whiptail --title "Error!" --msgbox "Tone count must be an integer in [2, 1024]!" 8 78
			abort_action=1
			return
		fi
	else
		abort_action=1
		return
	fi
fi

abort_action=0

}

do_enter_nfm_mode()
{

LAST_ITEM="$menuchoice"
if NFM_MODE=$(whiptail --default-item "$DEFAULT_NFM_MODE" --title "NFM deviation mode" --menu "Select NFM deviation mode:" 15 78 2 \
	"Wide" "+-5 kHz deviation for 25 kHz channels (amateur VHF/UHF)" \
	"Narrow" "+-2.5 kHz deviation for 12.5 kHz channels (PMR/DMR)" \
	3>&1 1>&2 2>&3); then
	abort_action=0
else
	abort_action=1
fi

}

do_enter_ssb_sideband()
{

LAST_ITEM="$menuchoice"
if SSB_SIDEBAND=$(whiptail --default-item "$DEFAULT_SSB_SIDEBAND" --title "SSB sideband" --menu "Select SSB sideband:" 15 78 2 \
	"USB" "Upper Side Band modulation" \
	"LSB" "Lower Side Band modulation" \
	3>&1 1>&2 2>&3); then
	abort_action=0
else
	abort_action=1
fi

}

do_enter_playback_mode()
{

LAST_ITEM="$menuchoice"
if PLAYBACK_MODE=$(whiptail --default-item "$DEFAULT_PLAYBACK" --title "Playback mode" --menu "Select playback mode:" 15 78 2 \
	"loop" "Replay the audio file continuously" \
	"once" "Play once and stop at end of file" \
	3>&1 1>&2 2>&3); then
	abort_action=0
else
	abort_action=1
fi

}

do_enter_rds_params()
{

LAST_ITEM="$menuchoice"

# PI code (Programme Identification): 1-4 hex digits, optional 0x prefix
if RDS_PI=$(whiptail --inputbox "Enter RDS Programme Identification (1-4 hex digits, optional 0x prefix):" 8 78 "$DEFAULT_RDS_PI" --title "RDS Programme Identification (PI)" 3>&1 1>&2 2>&3); then
	abort_action=0
	if [ -z "$RDS_PI" ] || ! [[ "$RDS_PI" =~ ^(0[xX])?[0-9a-fA-F]{1,4}$ ]]; then
		whiptail --title "Error!" --msgbox "PI must be 1-4 hex digits, optionally prefixed with 0x/0X!" 8 78
		abort_action=1
		return
	fi
else
	abort_action=1
	return
fi

# PS name (Programme Service): 1-8 ASCII chars (RDS does not carry non-ASCII text)
if RDS_PS=$(whiptail --inputbox "Enter RDS Programme Service name (1-8 ASCII chars):" 8 78 "$DEFAULT_RDS_PS" --title "RDS Programme Service (PS)" 3>&1 1>&2 2>&3); then
	abort_action=0
	if [ -z "$RDS_PS" ]; then
		whiptail --title "Error!" --msgbox "PS cannot be empty!" 8 78
		abort_action=1
		return
	elif printf '%s' "$RDS_PS" | LC_ALL=C grep -q '[^ -~]'; then
		whiptail --title "Error!" --msgbox "PS must contain only printable ASCII (0x20-0x7E); RDS does not carry non-ASCII text!" 8 78
		abort_action=1
		return
	elif [ "${#RDS_PS}" -gt 8 ]; then
		whiptail --title "Error!" --msgbox "PS must not exceed 8 characters!" 8 78
		abort_action=1
		return
	fi
else
	abort_action=1
	return
fi

# RT (RadioText): 1-64 ASCII chars (RDS does not carry non-ASCII text)
if RDS_RT=$(whiptail --inputbox "Enter RDS RadioText (1-64 ASCII chars):" 8 78 "$DEFAULT_RDS_RT" --title "RDS RadioText (RT)" 3>&1 1>&2 2>&3); then
	abort_action=0
	if [ -z "$RDS_RT" ]; then
		whiptail --title "Error!" --msgbox "RT cannot be empty!" 8 78
		abort_action=1
		return
	elif printf '%s' "$RDS_RT" | LC_ALL=C grep -q '[^ -~]'; then
		whiptail --title "Error!" --msgbox "RT must contain only printable ASCII (0x20-0x7E); RDS does not carry non-ASCII text!" 8 78
		abort_action=1
		return
	elif [ "${#RDS_RT}" -gt 64 ]; then
		whiptail --title "Error!" --msgbox "RT must not exceed 64 characters!" 8 78
		abort_action=1
		return
	fi
else
	abort_action=1
	return
fi

# Pre-emphasis time constant: 50 us (Eu) or 75 us (Us)
if RDS_PE=$(whiptail --default-item "$DEFAULT_RDS_PE" --title "FM pre-emphasis" --menu "Select pre-emphasis time constant:" 15 78 2 \
	"50" "50 us - Europe, Africa, Asia, Oceania (ITU regions 1/3)" \
	"75" "75 us - Americas, Japan (ITU region 2)" \
	3>&1 1>&2 2>&3); then
	abort_action=0
else
	abort_action=1
fi

}

do_enter_callsign()
{

LAST_ITEM="$menuchoice"
if CALLSIGN=$(whiptail --inputbox "Type callsign:" 8 78 "$DEFAULT_OPERA_CALLSIGN" --title "Enter callsign to transmit" 3>&1 1>&2 2>&3);  then
	abort_action=0
	if [ -z "$CALLSIGN" ]; then
    	whiptail --title "Error!" --msgbox "Empty callsign!" 8 78
		abort_action=1
	fi
else
	abort_action=1
fi

}

do_stop_transmit()
{
	sudo killall csdr 2>/dev/null
	sudo killall freedv 2>/dev/null
	sudo killall piam 2>/dev/null
	sudo killall pichirp 2>/dev/null
	sudo killall pifmrds 2>/dev/null
	sudo killall pimorse 2>/dev/null
	sudo killall pinfm 2>/dev/null
	sudo killall piopera 2>/dev/null
	sudo killall pirfgen 2>/dev/null
	sudo killall pirtty 2>/dev/null
	sudo killall pissb 2>/dev/null
	sudo killall pisstv 2>/dev/null
	sudo killall pocsag 2>/dev/null
	sudo killall rpitx 2>/dev/null
	sudo killall sendiq 2>/dev/null
	sudo killall spectrumpaint 2>/dev/null
	sudo killall tune 2>/dev/null

	case "$menuchoice" in
			
			0\ *) sudo killall testvfo.sh >/dev/null 2>/dev/null ;;
			1\ *) sudo killall testchirp.sh >/dev/null 2>/dev/null ;;
			2\ *) sudo killall testspectrum.sh >/dev/null 2>/dev/null ;; 
			3\ *) sudo killall snap2spectrum.sh >/dev/null 2>/dev/null ;;
			4\ *) sudo killall testfmrds.sh >/dev/null 2>/dev/null ;;
			5\ *) sudo killall testnfm.sh >/dev/null 2>/dev/null ;;
			6\ *) sudo killall testssb.sh >/dev/null 2>/dev/null ;;
			7\ *) sudo killall testam.sh >/dev/null 2>/dev/null ;;
			8\ *) sudo killall testfreedv.sh >/dev/null 2>/dev/null ;;
			9\ *) sudo killall testsstv.sh >/dev/null 2>/dev/null ;;
			10\ *) sudo killall testpocsag.sh >/dev/null 2>/dev/null ;;
			11\ *) sudo killall testopera.sh >/dev/null 2>/dev/null ;;
			12\ *) sudo killall testrtty.sh >/dev/null 2>/dev/null ;;
			13\ *) sudo killall testmorse.sh >/dev/null 2>/dev/null ;;
			14\ *) sudo killall testrfgen.sh >/dev/null 2>/dev/null ;;

	esac
}

do_status()
{
	LAST_ITEM="$menuchoice"
	whiptail --title "Transmit ""$LAST_ITEM"" on ""$OUTPUT_FREQ"" MHz" --msgbox "Transmitting" 8 78
	do_stop_transmit
}

#********************************
# User interface initialization *
#********************************

do_freq_setup

 while [ true ]
    do

	menuchoice=$(whiptail --default-item "$LAST_ITEM" --title "rpitx-ui on ""$OUTPUT_FREQ"" MHz" --menu "Range frequency: 50kHz-1GHz. Choose your test:" 20 82 12 \
 	"F Set frequency" "Modify frequency (actual $OUTPUT_FREQ MHz)" \
	"0 Tune" "Carrier" \
    "1 Chirp" "Moving carrier" \
	"2 Spectrum" "Spectrum painting" \
	"3 RfMyFace" "Snap with Raspicam and RF paint" \
	"4 WFM" "Wideband Frequency Modulation with RDS" \
	"5 NFM" "Narrowband Frequency Modulation" \
	"6 SSB" "Single Sideband modulation" \
	"7 AM" "Amplitude Modulation" \
	"8 FreeDV" "Digital voice mode 800XA" \
	"9 SSTV" "Pattern picture" \
	"10 Pocsag" "Pager message" \
    "11 Opera" "Like morse but need Opera decoder" \
    "12 RTTY" "Radioteletype" \
    "13 CW" "Continuous Wave (Morse code)" \
    "14 Generator" "Wideband RF generator" \
 	3>&2 2>&1 1>&3)
		RET=$?
		if [ $RET -eq 1 ]; then
			whiptail --title "Bye bye" --msgbox "Thanks for using rpitx-ui!" 8 78
    		exit 0
		elif [ $RET -eq 0 ]; then
			case "$menuchoice" in
			
			F\ *) do_freq_setup 
			;;
			
			0\ *) testvfo.sh "$OUTPUT_FREQ""e6" >/dev/null 2>/dev/null &
			do_status
			;;
			
			1\ *) testchirp.sh "$OUTPUT_FREQ""e6" >/dev/null 2>/dev/null &
			do_status
			;;
			
			2\ *) do_file_choose "320x256 .jpg" "$RESOURCES_LOCATION" '\.jpg$'
			if [ $abort_action -eq 0 ]; then
				testspectrum.sh "$OUTPUT_FREQ""e6" "$FILE_LOC" >/dev/null 2>/dev/null &
				do_status
			fi
			;;
			
			3\ *) snap2spectrum.sh "$OUTPUT_FREQ""e6" >/dev/null 2>/dev/null &
			do_status
			;;
			
			4\ *) do_file_choose "audio (.aif, .aiff, .caf, .flac, .mp3, .wav)" "$RESOURCES_LOCATION" "$AUDIO_FILE_PATTERN"
			if [ $abort_action -eq 0 ]; then
				do_enter_playback_mode
				if [ $abort_action -eq 0 ]; then
					do_enter_rds_params
					if [ $abort_action -eq 0 ]; then
						testfmrds.sh "$OUTPUT_FREQ""e6" "$FILE_LOC" "$PLAYBACK_MODE" "$RDS_PI" "$RDS_PS" "$RDS_RT" "$RDS_PE" >/dev/null 2>/dev/null &
						do_status
					fi
				fi
			fi
			;;

			5\ *) do_file_choose "audio (.aif, .aiff, .caf, .flac, .mp3, .wav)" "$RESOURCES_LOCATION" "$AUDIO_FILE_PATTERN"
			if [ $abort_action -eq 0 ]; then
				do_enter_playback_mode
				if [ $abort_action -eq 0 ]; then
					do_enter_nfm_mode
					if [ $abort_action -eq 0 ]; then
						testnfm.sh "$OUTPUT_FREQ""e6" "$FILE_LOC" "$PLAYBACK_MODE" "${NFM_MODE,,}" >/dev/null 2>/dev/null &
						do_status
					fi
				fi
			fi
			;;
			
			6\ *) do_file_choose "audio (.aif, .aiff, .caf, .flac, .mp3, .wav)" "$RESOURCES_LOCATION" "$AUDIO_FILE_PATTERN"
			if [ $abort_action -eq 0 ]; then
				do_enter_playback_mode
				if [ $abort_action -eq 0 ]; then
					do_enter_ssb_sideband
					if [ $abort_action -eq 0 ]; then
						testssb.sh "$OUTPUT_FREQ""e6" "$FILE_LOC" "$PLAYBACK_MODE" "${SSB_SIDEBAND,,}" >/dev/null 2>/dev/null &
						do_status
					fi
				fi
			fi
			;;
			
			7\ *) do_file_choose "audio (.aif, .aiff, .caf, .flac, .mp3, .wav)" "$RESOURCES_LOCATION" "$AUDIO_FILE_PATTERN"
			if [ $abort_action -eq 0 ]; then
				do_enter_playback_mode
				if [ $abort_action -eq 0 ]; then
					testam.sh "$OUTPUT_FREQ""e6" "$FILE_LOC" "$PLAYBACK_MODE" >/dev/null 2>/dev/null &
					do_status
				fi
			fi
			;;
			
			8\ *) do_file_choose "FreeDV .rf" "$RESOURCES_LOCATION" '\.rf$'
			if [ $abort_action -eq 0 ]; then
				testfreedv.sh "$OUTPUT_FREQ""e6" "$FILE_LOC" >/dev/null 2>/dev/null &
				do_status
			fi
			;;
			
			9\ *) do_file_choose "320x256 .jpg" "$RESOURCES_LOCATION" '\.jpg$'
			if [ $abort_action -eq 0 ]; then
				testsstv.sh "$OUTPUT_FREQ""e6" "$FILE_LOC" >/dev/null 2>/dev/null &
				do_status
			fi
			;;
			
			10\ *) do_enter_message "POCSAG (ADDR:MESSAGE_BODY)" "$DEFAULT_POCSAG_MESSAGE"
			if [ $abort_action -eq 0 ]; then
				testpocsag.sh "$OUTPUT_FREQ""e6" "$MESSAGE" >/dev/null 2>/dev/null &
				do_status
			fi
			;;

			11\ *) do_enter_callsign
			if [ $abort_action -eq 0 ]; then
				testopera.sh "$OUTPUT_FREQ""e6" "$CALLSIGN" >/dev/null 2>/dev/null &
				do_status
			fi
			;;

			12\ *) do_enter_message "RTTY" "$DEFAULT_RTTY_MESSAGE"
			if [ $abort_action -eq 0 ]; then
				testrtty.sh "$OUTPUT_FREQ""e6" "$MESSAGE" >/dev/null 2>/dev/null &
				do_status
			fi
			;;

			13\ *) do_enter_message "CW" "$DEFAULT_CW_MESSAGE"
			if [ $abort_action -eq 0 ]; then
				do_enter_wpm
				if [ $abort_action -eq 0 ]; then
					testmorse.sh "$OUTPUT_FREQ""e6" "$CW_WPM" "$MESSAGE" >/dev/null 2>/dev/null &
					do_status
				fi
			fi
			;;

			14\ *) do_enter_rfgen_params
			if [ $abort_action -eq 0 ]; then
				testrfgen.sh "$OUTPUT_FREQ""e6" "$RFGEN_BW" "$DEFAULT_RFGEN_SAMPLE_RATE" "${RFGEN_MODE,,}" "$MULTITONE_TONES" >/dev/null 2>/dev/null &
				do_status
			fi
			;;

			esac
		else
			exit 1
		fi
    done
	exit 0
