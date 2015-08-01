#ifndef __M_STR__
#define __M_STR__

// simply cross-platform string functions


int	id_strcasecmp(const char* s0, const char* s1);
int	id_strncasecmp(const char* s0, const char* s1, int n);
void	id_strupr(char* s);

#endif//__M_STR__