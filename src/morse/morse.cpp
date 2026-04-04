#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <librpitx/librpitx.h>

#define MORSECODES 37

typedef struct morse_code
{
	uint8_t ch;
	const char dits[8];
} Morsecode;

const Morsecode code_table[]  =
{
	{' ', "    "}, // space, 4 dits
	{'0', "-----  "},
	{'1', ".----  "},
	{'2', "..---  "},
	{'3', "...--  "},
	{'4', "....-  "},
	{'5', ".....  "},
	{'6', "-....  "},
	{'7', "--...  "},
	{'8', "---..  "},
	{'9', "----.  "},
	{'A', ".-  "},
	{'B', "-...  "},
	{'C', "-.-.  "},
	{'D', "-..  "},
	{'E', ".  "},
	{'F', "..-.  "},
	{'G', "--.  "},
	{'H', "....  "},
	{'I', "..  "},
	{'J', ".---  "},
	{'K', "-.-  "},
	{'L', ".-..  "},
	{'M', "--  "},
	{'N', "-.  "},
	{'O', "---  "},
	{'P', ".--.  "},
	{'Q', "--.-  "},
	{'R', ".-.  "},
	{'S', "...  "},
	{'T', "-  "},
	{'U', "..-  "},
	{'V', "...-  "},
	{'W', ".--  "},
	{'X', "-..-  "},
	{'Y', "-.--  "},
	{'Z', "--..  "}
};

/**
    Transmits CW OOK data at given rate and frequency.
 */
void Send_CW_OOK(const float freq, const float symbolrate, const char * cw)
{
	float upsample = 125.0;
	int FifoSize = strlen((char*)cw) - 1;
	ookburst ook(freq, symbolrate, 14, FifoSize, upsample);

	unsigned char TabSymbol[FifoSize + 1];
	for (int i = 0; i <= FifoSize; i++)
	{
		TabSymbol[i] = (cw[i] == '0') ? 0 : 1;
	}
	ook.SetSymbols(TabSymbol, FifoSize);
}

void morse_to_cw(const char * dits, char * cw)
{
	int dits_len = strlen(dits);
	int a = 0;
	for (int i = 0; i < dits_len; i++)
	{
		if (dits[i] == '.')
		{
			cw[a++] = '1';
			cw[a++] = '0';
		}
		else if (dits[i] == '-')		
		{
			cw[a++] = '1';
			cw[a++] = '1';
			cw[a++] = '1';
			cw[a++] = '0';
		}
		else if (dits[i] == ' ')
		{
			cw[a++] = '0';
		}
	}
	cw[a] = '\0';
}

const char * text_to_morse(const char txt)
{
	char tch = toupper(txt);
	for (int j = 0; j < MORSECODES; j++)
	{
		if (code_table[j].ch == tch)
		{
			return code_table[j].dits;
		}
	}
	return NULL;
}

int main(int argc, char * argv[])
{
	const char * msg = "TEST MESSAGEZ1 MSG2 MSG3 MSG0 MSG9";
	float freq = 433000000;
	float wpm = 5;

	if (argc < 4)
	{
		printf("usage: ./morse freq(Hz) rate(dits) MSG(\"quoted\")\n");
		exit(0);
	}

	freq = atof(argv[1]);
	wpm = atof(argv[2]);
	if (freq <= 0 || wpm <= 0)
	{
		printf("Error: freq and rate must be positive values\n");
		exit(1);
	}
	msg = argv[3];
	printf("msg: %s\n", msg);
	char cw[23];
	const float symbol_rate = wpm/1.25;
	const size_t msg_len = strlen(msg);

	for (size_t i = 0; i < msg_len; i++)
	{
		const char * dits = text_to_morse(msg[i]);
		if (dits == NULL)
		{
			printf("msg[%02zu]: %c\tskipped (unsupported character)\n", i, msg[i]);
			continue;
		}
		morse_to_cw(dits, cw);
		printf("msg[%02zu]: %c\tmorse[%s]\tcw[%s]\n", i, toupper(msg[i]), dits, cw);
		Send_CW_OOK(freq, symbol_rate, cw);
	}
}
