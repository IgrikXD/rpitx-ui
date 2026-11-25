#!/bin/bash
abort_action=0

OUTPUT_FREQ=434.0
RESOURCES_LOCATION="${RPITX_RESOURCES_LOCATION:-/usr/share/rpitx-ui}"
DEFAULT_POCSAG_ADDRESS="1234567"
DEFAULT_POCSAG_MESSAGE="Hello world from rpitx-ui"
DEFAULT_POCSAG_NUMERIC_MESSAGE="0123456789"
DEFAULT_POCSAG_TYPE="Alphanumeric"
DEFAULT_POCSAG_MODE="POCSAG-512"
DEFAULT_OPERA_CALLSIGN="Q0CAL"
DEFAULT_OPERA_MODE="Op05"
DEFAULT_RTTY_MESSAGE="HELLO WORLD FROM RPITX"
DEFAULT_RTTY_MODE="RTTY-45"
DEFAULT_CW_MESSAGE="CQ CQ DE RPITX"
DEFAULT_CW_WPM=5
DEFAULT_FOX_CODE="MO"
DEFAULT_FOX_MODE="A1A"
DEFAULT_FOX_WPM=8
DEFAULT_FOX_CALLSIGN=""
DEFAULT_RFGEN_SAMPLE_RATE=500000
DEFAULT_RFGEN_BANDWIDTH=200000
DEFAULT_MULTITONE_TONES=8
DEFAULT_NFM_MODE="Wide"
DEFAULT_PLAYBACK="loop"
DEFAULT_SSB_SIDEBAND="USB"
DEFAULT_SSTV_MODE="Martin-M1"
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
	elif ! printf '%s' "$CW_WPM" | LC_ALL=C grep -Eq '^0*[1-9][0-9]{0,2}$' || [ "$CW_WPM" -gt 200 ]; then
		whiptail --title "Error!" --msgbox "WPM must be an integer in [1, 200]!" 8 78
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

do_enter_sstv_mode()
{

LAST_ITEM="$menuchoice"
if SSTV_MODE=$(whiptail --default-item "$DEFAULT_SSTV_MODE" --title "SSTV mode" --menu "Select SSTV mode:" 17 78 9 \
	"Martin-M1" "320x256 picture in 114 seconds" \
	"Martin-M2" "320x256 picture in 58 seconds" \
	"Scottie-S1" "320x256 picture in 110 seconds" \
	"Scottie-S2" "320x256 picture in 71 seconds" \
	"Robot-36" "320x240 picture in 36 seconds" \
	"Robot-72" "320x240 picture in 72 seconds" \
	"PD-90" "320x256 picture in 90 seconds" \
	"PD-120" "640x496 picture in 126 seconds" \
	"PD-180" "640x496 picture in 187 seconds" \
	3>&1 1>&2 2>&3); then
	abort_action=0
else
	abort_action=1
fi

}

do_enter_pocsag_params()
{

LAST_ITEM="$menuchoice"
MESSAGE=""

# Pager address: decimal, without the addresses of the idle and of the synchronization codeword
if POCSAG_ADDRESS=$(whiptail --inputbox "Enter pager address (RIC, 0-2097151):" 8 78 "$DEFAULT_POCSAG_ADDRESS" --title "POCSAG pager address" 3>&1 1>&2 2>&3); then
	if ! printf '%s' "$POCSAG_ADDRESS" | LC_ALL=C grep -Eq '^[0-9]{1,7}$' || [ "$POCSAG_ADDRESS" -gt 2097151 ]; then
		whiptail --title "Error!" --msgbox "Address must be an integer in [0, 2097151]!" 8 78
		abort_action=1
		return
	elif [ "$POCSAG_ADDRESS" -ge 2007664 ] && [ "$POCSAG_ADDRESS" -le 2007671 ]; then
		whiptail --title "Error!" --msgbox "Addresses 2007664-2007671 belong to the idle codeword!" 8 78
		abort_action=1
		return
	elif [ "$POCSAG_ADDRESS" -ge 2045056 ] && [ "$POCSAG_ADDRESS" -le 2045063 ]; then
		whiptail --title "Error!" --msgbox "Addresses 2045056-2045063 belong to the synchronization codeword!" 8 78
		abort_action=1
		return
	fi
else
	abort_action=1
	return
fi

# Message type
if ! POCSAG_TYPE=$(whiptail --default-item "$DEFAULT_POCSAG_TYPE" --title "POCSAG message type" --menu "Select POCSAG message type:" 15 78 3 \
	"Alphanumeric" "Text in 7-bit ASCII" \
	"Numeric" "Digits, space, U, - and brackets" \
	"Tone-only" "Call without a message" \
	3>&1 1>&2 2>&3); then
	abort_action=1
	return
fi

# Message of the selected type; the standard assigns function A to numeric and D to alphanumeric messages
POCSAG_DEFAULT_FUNCTION="A"
if [ "$POCSAG_TYPE" = "Numeric" ]; then
	do_enter_message "POCSAG" "$DEFAULT_POCSAG_NUMERIC_MESSAGE"
	if [ $abort_action -ne 0 ]; then
		return
	elif printf '%s' "$MESSAGE" | LC_ALL=C grep -q '[^][0-9U ()-]'; then
		whiptail --title "Error!" --msgbox "Numeric message must contain only digits, spaces, U, - and brackets!" 8 78
		abort_action=1
		return
	fi
elif [ "$POCSAG_TYPE" = "Alphanumeric" ]; then
	POCSAG_DEFAULT_FUNCTION="D"
	do_enter_message "POCSAG" "$DEFAULT_POCSAG_MESSAGE"
	if [ $abort_action -ne 0 ]; then
		return
	elif printf '%s' "$MESSAGE" | LC_ALL=C grep -q '[^ -~]'; then
		whiptail --title "Error!" --msgbox "Alphanumeric message must contain only printable ASCII (0x20-0x7E)!" 8 78
		abort_action=1
		return
	fi
fi

# Function: one of the four addresses of the pager
if ! POCSAG_FUNCTION=$(whiptail --default-item "$POCSAG_DEFAULT_FUNCTION" --title "POCSAG function" --menu "Select POCSAG function (address A-D of the pager):" 15 78 4 \
	"A" "Function bits 00 (numeric messages by the standard)" \
	"B" "Function bits 01" \
	"C" "Function bits 10" \
	"D" "Function bits 11 (alphanumeric messages by the standard)" \
	3>&1 1>&2 2>&3); then
	abort_action=1
	return
fi

# POCSAG mode: the bit rate
if POCSAG_MODE=$(whiptail --default-item "$DEFAULT_POCSAG_MODE" --title "POCSAG mode" --menu "Select POCSAG mode:" 15 78 3 \
	"POCSAG-512" "512 bit/s" \
	"POCSAG-1200" "1200 bit/s" \
	"POCSAG-2400" "2400 bit/s" \
	3>&1 1>&2 2>&3); then
	abort_action=0
else
	abort_action=1
fi

}

do_check_rtty_message()
{

# Characters of the ITA2 alphabet only (letters are sent in upper case)
if printf '%s' "$MESSAGE" | LC_ALL=C grep -q "[^[:alnum:] !\"#\$&'(),./:;?-]"; then
	whiptail --title "Error!" --msgbox "Message must contain only characters of the ITA2 alphabet (Latin letters, digits, space and ! \" # \$ & ' ( ) , - . / : ; ?)!" 8 78
	return 1
fi
return 0

}

do_enter_rtty_mode()
{

LAST_ITEM="$menuchoice"
if RTTY_MODE=$(whiptail --default-item "$DEFAULT_RTTY_MODE" --title "RTTY mode" --menu "Select RTTY mode (170 Hz shift):" 15 78 3 \
	"RTTY-45" "45.45 baud (60 WPM)" \
	"RTTY-50" "50 baud (66 WPM)" \
	"RTTY-75" "75 baud (100 WPM)" \
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
	"loop" "Replay the file continuously" \
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
	elif ! printf '%s' "$CALLSIGN" | LC_ALL=C grep -Eq '^[A-Za-z0-9]{1,2}[0-9][A-Za-z]{1,3}$'; then
		whiptail --title "Error!" --msgbox "Callsign must be one or two letters or digits, a digit and one to three letters!" 8 78
		abort_action=1
	fi
else
	abort_action=1
fi

}

do_enter_opera_mode()
{

LAST_ITEM="$menuchoice"
if OPERA_MODE=$(whiptail --default-item "$DEFAULT_OPERA_MODE" --title "Opera mode" --menu "Select Opera mode:" 15 78 7 \
	"Op05" "Callsign in 31 seconds" \
	"Op1" "Callsign in 61 seconds" \
	"Op2" "Callsign in 2 minutes" \
	"Op4" "Callsign in 4 minutes" \
	"Op8" "Callsign in 8 minutes" \
	"Op16" "Callsign in 16 minutes" \
	"Op32" "Callsign in 33 minutes" \
	3>&1 1>&2 2>&3); then
	abort_action=0
else
	abort_action=1
fi

}

do_enter_fox_params()
{

LAST_ITEM="$menuchoice"

# Transmitter of the classic ARDF competition: the code it sends and its operating period
if ! FOX_CODE=$(whiptail --default-item "$DEFAULT_FOX_CODE" --title "Foxhunt transmitter" --menu "Select transmitter code:" 15 78 6 \
	"MO" "Finish beacon, continuously" \
	"MOE" "Transmitter 1, first minute of every 5 minutes" \
	"MOI" "Transmitter 2, second minute of every 5 minutes" \
	"MOS" "Transmitter 3, third minute of every 5 minutes" \
	"MOH" "Transmitter 4, fourth minute of every 5 minutes" \
	"MO5" "Transmitter 5, fifth minute of every 5 minutes" \
	3>&1 1>&2 2>&3); then
	abort_action=1
	return
fi

# Emission mode
if ! FOX_MODE=$(whiptail --default-item "$DEFAULT_FOX_MODE" --title "Foxhunt emission mode" --menu "Select emission mode:" 15 78 2 \
	"A1A" "Keyed carrier (3.5 MHz band)" \
	"A2A" "Keyed carrier modulated by a 1000 Hz tone (144 MHz band)" \
	3>&1 1>&2 2>&3); then
	abort_action=1
	return
fi

# Keying speed: the range of the IARU ARDF rules
if FOX_WPM=$(whiptail --inputbox "Enter keying speed (WPM, 8-15):" 8 78 "$DEFAULT_FOX_WPM" --title "Foxhunt keying speed" 3>&1 1>&2 2>&3); then
	if ! printf '%s' "$FOX_WPM" | LC_ALL=C grep -Eq '^0*([89]|1[0-5])$'; then
		whiptail --title "Error!" --msgbox "WPM must be an integer in [8, 15]!" 8 78
		abort_action=1
		return
	fi
else
	abort_action=1
	return
fi

# Optional callsign: DE and the callsign end every minute of operation
if FOX_CALLSIGN=$(whiptail --inputbox "Enter callsign to identify the transmitter (empty for none):" 8 78 "$DEFAULT_FOX_CALLSIGN" --title "Foxhunt callsign" 3>&1 1>&2 2>&3); then
	if [ -n "$FOX_CALLSIGN" ] && ! printf '%s' "$FOX_CALLSIGN" | LC_ALL=C grep -Eq '^[A-Za-z0-9/]{1,15}$'; then
		whiptail --title "Error!" --msgbox "Callsign must be 1-15 letters, digits or / (empty for none)!" 8 78
		abort_action=1
		return
	fi
else
	abort_action=1
	return
fi

abort_action=0

}

do_stop_transmit()
{
	sudo killall csdr 2>/dev/null
	sudo killall piam 2>/dev/null
	sudo killall pichirp 2>/dev/null
	sudo killall pifmrds 2>/dev/null
	sudo killall pifoxhunt 2>/dev/null
	sudo killall pimorse 2>/dev/null
	sudo killall pinfm 2>/dev/null
	sudo killall piopera 2>/dev/null
	sudo killall pipocsag 2>/dev/null
	sudo killall pirfgen 2>/dev/null
	sudo killall pirtty 2>/dev/null
	sudo killall pispectrumpaint 2>/dev/null
	sudo killall pissb 2>/dev/null
	sudo killall pisstv 2>/dev/null
	sudo killall pitune 2>/dev/null
	sudo killall rpitx 2>/dev/null
	sudo killall sendiq 2>/dev/null

	case "$menuchoice" in
			
			0\ *) sudo killall testtune.sh >/dev/null 2>/dev/null ;;
			1\ *) sudo killall testchirp.sh >/dev/null 2>/dev/null ;;
			2\ *) sudo killall testspectrum.sh >/dev/null 2>/dev/null ;; 
			3\ *) sudo killall snap2spectrum.sh >/dev/null 2>/dev/null ;;
			4\ *) sudo killall testfmrds.sh >/dev/null 2>/dev/null ;;
			5\ *) sudo killall testnfm.sh >/dev/null 2>/dev/null ;;
			6\ *) sudo killall testssb.sh >/dev/null 2>/dev/null ;;
			7\ *) sudo killall testam.sh >/dev/null 2>/dev/null ;;
			8\ *) sudo killall testsstv.sh >/dev/null 2>/dev/null ;;
			9\ *) sudo killall testpocsag.sh >/dev/null 2>/dev/null ;;
			10\ *) sudo killall testopera.sh >/dev/null 2>/dev/null ;;
			11\ *) sudo killall testrtty.sh >/dev/null 2>/dev/null ;;
			12\ *) sudo killall testmorse.sh >/dev/null 2>/dev/null ;;
			13\ *) sudo killall testrfgen.sh >/dev/null 2>/dev/null ;;
			14\ *) sudo killall testfoxhunt.sh >/dev/null 2>/dev/null ;;

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
	"0 Tune" "Unmodulated carrier" \
    "1 Chirp" "Moving carrier" \
	"2 Spectrum" "Spectrum painting" \
	"3 RfMyFace" "Snap with Raspicam and RF paint" \
	"4 WFM" "Wideband Frequency Modulation with RDS" \
	"5 NFM" "Narrowband Frequency Modulation" \
	"6 SSB" "Single Sideband modulation" \
	"7 AM" "Amplitude Modulation" \
	"8 SSTV" "Slow-Scan Television" \
	"9 Pocsag" "Pager message" \
    "10 Opera" "Weak-signal ASK beacon" \
    "11 RTTY" "Radioteletype" \
    "12 CW" "Continuous Wave (Morse code)" \
    "13 RFgen" "Wideband RF generator" \
    "14 Foxhunt" "Fox hunting transmitter (ARDF)" \
 	3>&2 2>&1 1>&3)
		RET=$?
		if [ $RET -eq 1 ]; then
			whiptail --title "Bye bye" --msgbox "Thanks for using rpitx-ui!" 8 78
    		exit 0
		elif [ $RET -eq 0 ]; then
			case "$menuchoice" in
			
			F\ *) do_freq_setup 
			;;
			
			0\ *) testtune.sh "$OUTPUT_FREQ""e6" >/dev/null 2>/dev/null &
			do_status
			;;
			
			1\ *) testchirp.sh "$OUTPUT_FREQ""e6" >/dev/null 2>/dev/null &
			do_status
			;;
			
			2\ *) do_file_choose ".jpg" "$RESOURCES_LOCATION" '\.jpg$'
			if [ $abort_action -eq 0 ]; then
				do_enter_playback_mode
				if [ $abort_action -eq 0 ]; then
					testspectrum.sh "$OUTPUT_FREQ""e6" "$FILE_LOC" "$PLAYBACK_MODE" >/dev/null 2>/dev/null &
					do_status
				fi
			fi
			;;
			
			3\ *) snap2spectrum.sh "$OUTPUT_FREQ""e6" >/dev/null 2>/dev/null &
			do_status
			;;
			
			4\ *) do_file_choose "audio (.aif, .aiff, .caf, .flac, .mp3, .wav)" "$RESOURCES_LOCATION" "$AUDIO_FILE_PATTERN"
			if [ $abort_action -eq 0 ]; then
				do_enter_rds_params
				if [ $abort_action -eq 0 ]; then
					do_enter_playback_mode
					if [ $abort_action -eq 0 ]; then
						testfmrds.sh "$OUTPUT_FREQ""e6" "$FILE_LOC" "$PLAYBACK_MODE" "$RDS_PI" "$RDS_PS" "$RDS_RT" "$RDS_PE" >/dev/null 2>/dev/null &
						do_status
					fi
				fi
			fi
			;;

			5\ *) do_file_choose "audio (.aif, .aiff, .caf, .flac, .mp3, .wav)" "$RESOURCES_LOCATION" "$AUDIO_FILE_PATTERN"
			if [ $abort_action -eq 0 ]; then
				do_enter_nfm_mode
				if [ $abort_action -eq 0 ]; then
					do_enter_playback_mode
					if [ $abort_action -eq 0 ]; then
						testnfm.sh "$OUTPUT_FREQ""e6" "$FILE_LOC" "$PLAYBACK_MODE" "${NFM_MODE,,}" >/dev/null 2>/dev/null &
						do_status
					fi
				fi
			fi
			;;
			
			6\ *) do_file_choose "audio (.aif, .aiff, .caf, .flac, .mp3, .wav)" "$RESOURCES_LOCATION" "$AUDIO_FILE_PATTERN"
			if [ $abort_action -eq 0 ]; then
				do_enter_ssb_sideband
				if [ $abort_action -eq 0 ]; then
					do_enter_playback_mode
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
			
			8\ *) do_file_choose ".jpg" "$RESOURCES_LOCATION" '\.jpg$'
			if [ $abort_action -eq 0 ]; then
				do_enter_sstv_mode
				if [ $abort_action -eq 0 ]; then
					do_enter_playback_mode
					if [ $abort_action -eq 0 ]; then
						testsstv.sh "$OUTPUT_FREQ""e6" "$FILE_LOC" "$PLAYBACK_MODE" "${SSTV_MODE,,}" >/dev/null 2>/dev/null &
						do_status
					fi
				fi
			fi
			;;
			
			9\ *) do_enter_pocsag_params
			if [ $abort_action -eq 0 ]; then
				testpocsag.sh "$OUTPUT_FREQ""e6" "$POCSAG_ADDRESS" "$MESSAGE" "${POCSAG_TYPE,,}" "${POCSAG_MODE#POCSAG-}" "${POCSAG_FUNCTION,,}" >/dev/null 2>/dev/null &
				do_status
			fi
			;;

			10\ *) do_enter_callsign
			if [ $abort_action -eq 0 ]; then
				do_enter_opera_mode
				if [ $abort_action -eq 0 ]; then
					testopera.sh "$OUTPUT_FREQ""e6" "$CALLSIGN" "${OPERA_MODE,,}" >/dev/null 2>/dev/null &
					do_status
				fi
			fi
			;;

			11\ *) do_enter_message "RTTY" "$DEFAULT_RTTY_MESSAGE"
			if [ $abort_action -eq 0 ] && do_check_rtty_message; then
				do_enter_rtty_mode
				if [ $abort_action -eq 0 ]; then
					testrtty.sh "$OUTPUT_FREQ""e6" "$MESSAGE" "${RTTY_MODE#RTTY-}" >/dev/null 2>/dev/null &
					do_status
				fi
			fi
			;;

			12\ *) do_enter_message "CW" "$DEFAULT_CW_MESSAGE"
			if [ $abort_action -eq 0 ]; then
				do_enter_wpm
				if [ $abort_action -eq 0 ]; then
					testmorse.sh "$OUTPUT_FREQ""e6" "$CW_WPM" "$MESSAGE" >/dev/null 2>/dev/null &
					do_status
				fi
			fi
			;;

			13\ *) do_enter_rfgen_params
			if [ $abort_action -eq 0 ]; then
				testrfgen.sh "$OUTPUT_FREQ""e6" "$RFGEN_BW" "$DEFAULT_RFGEN_SAMPLE_RATE" "${RFGEN_MODE,,}" "$MULTITONE_TONES" >/dev/null 2>/dev/null &
				do_status
			fi
			;;

			14\ *) do_enter_fox_params
			if [ $abort_action -eq 0 ]; then
				testfoxhunt.sh "$OUTPUT_FREQ""e6" "$FOX_CODE" "$FOX_MODE" "$FOX_WPM" "$FOX_CALLSIGN" >/dev/null 2>/dev/null &
				do_status
			fi
			;;

			esac
		else
			exit 1
		fi
    done
	exit 0
