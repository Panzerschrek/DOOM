#include <stdlib.h>
#include <ctype.h>

int id_strncasecmp(const char* s0, const char* s1, int n)
{
    int i = 0;
    int s0_c, s1_c;
    while(*s0 && *s1 && i < n)
    {
	s0_c = toupper(*s0);
	s1_c = toupper(*s1);
	if (s0_c != s1_c) return s0_c - s1_c;
	s0++;
	s1++;
	i++;
    }
    return  i == n ? 0 : toupper(*s0) - toupper(*s1);
}

int id_strcasecmp(const char* s0, const char* s1)
{
    int s0_c, s1_c;
    while(*s0 && *s1)
    {
	s0_c = toupper(*s0);
	s1_c = toupper(*s1);
	if (s0_c != s1_c) return s0_c - s1_c;
	s0++;
	s1++;
    }
    return toupper(*s0) - toupper(*s1);
}

void id_strupr(char* s)
{
    while (*s) { *s = toupper(*s); s++; }
}