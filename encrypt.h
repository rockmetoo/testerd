//=============================================================================
//	File name: encrypt.h
//	Description:
//	Tab-Width: 4
//  Author: rockmetoo
//-----------------------------------------------------------------------------

#ifndef __encrypt_h__
#define __encrypt_h__

#ifdef __cplusplus
extern "C" {
#endif

#include <string.h>
#include <stdio.h>
#include <malloc.h>

unsigned short pkax = 0, pkbx = 0, pkcx = 0, pkdx = 0, pksi = 0, pktmp = 0, x1a2 = 0;
unsigned short pkres = 0, pki = 0, inter = 0, cfc = 0, cfd = 0, compte = 0;
unsigned short x1a0[8] = {0};
unsigned char cle[17] = {0};
short pkc = 0, plainlen, ascipherlen;

char* plainText = NULL;
char* ascCipherText = NULL;

void pkfin(void);
void pkcode(void);
void pkassemble(void);

void ascii_encrypt128(char* in, char* key);
void ascii_decrypt128(char* in, char* key);

#ifdef __cplusplus
}
#endif

#endif
//=============================================================================
//End of program
//-----------------------------------------------------------------------------
