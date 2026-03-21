#ifndef _CTYPE_H
#define _CTYPE_H

static inline int isspace(int ch)
{
	return (unsigned int)(ch - 9) < 5u || ch == ' ';
}

#endif /* _CTYPE_H */
