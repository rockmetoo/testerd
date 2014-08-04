#include <stdio.h>
#include <stdlib.h>
#include "encrypt.h"

#define	COMMAND_KEY		"40complex#$!11JY"

int main()
{

	char szControlCode[128] = {0};
	char key[17]			= {0};

	sprintf(szControlCode, "11");
	strcpy(key, COMMAND_KEY);

	plainlen = strlen(szControlCode);
	ascii_encrypt128(szControlCode, key);

	printf("encrypted: %s\n", ascCipherText);

	char* pszControlCode = ascCipherText;
	size_t plainlen = strlen(pszControlCode);

	// ascipherlen is decleared in encrypt.h
	ascipherlen = 2 * plainlen;
	ascii_decrypt128(pszControlCode, key);
	printf("HAHAHAHHA decrypt: %d\n", atoi(plainText));

	pszControlCode = NULL;


	return 1;
}
