/* prf.c */

/*
 * Copyright (c) 1997-2010, 2012-2015 Wind River Systems, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

#ifndef MAXFLD
#define	MAXFLD	200
#endif

#ifndef EOF
#define EOF  -1
#endif

static void _uc(char *buf)
{
	for (/**/; *buf; buf++) {
		if (*buf >= 'a' && *buf <= 'z') {
			*buf += 'A' - 'a';
		}
	}
}

/* Convention note: "end" as passed in is the standard "byte after
 * last character" style, but...
 */
static int _reverse_and_pad(char *start, char *end, int minlen)
{
	int len;

	while (end - start < minlen) {
		*end++ = '0';
	}

	*end = 0;
	len = end - start;
	for (end--; end > start; end--, start++) {
		char tmp = *end;
		*end = *start;
		*start = tmp;
	}
	return len;
}

/* Writes the specified number into the buffer in the given base,
 * using the digit characters 0-9a-z (i.e. base>36 will start writing
 * odd bytes), padding with leading zeros up to the minimum length.
 */
static int _to_x(char *buf, uint32_t n, int base, int minlen)
{
	char *buf0 = buf;

	do {
		int d = n % base;

		n /= base;
		*buf++ = '0' + d + (d > 9 ? ('a' - '0' - 10) : 0);
	} while (n);
	return _reverse_and_pad(buf0, buf, minlen);
}

static int _to_hex(char *buf, uint32_t value,
		   int alt_form, int precision, int prefix)
{
	int len;
	char *buf0 = buf;

	if (alt_form) {
		*buf++ = '0';
		*buf++ = 'x';
	}

	len = _to_x(buf, value, 16, precision);
	if (prefix == 'X') {
		_uc(buf0);
	}

	return len + (buf - buf0);
}

static int _to_octal(char *buf, uint32_t value, int alt_form, int precision)
{
	char *buf0 = buf;

	if (alt_form) {
		*buf++ = '0';
		if (!value) {
			/* So we don't return "00" for a value == 0. */
			*buf++ = 0;
			return 1;
		}
	}
	return (buf - buf0) + _to_x(buf, value, 8, precision);
}

static int _to_udec(char *buf, uint32_t value, int precision)
{
	return _to_x(buf, value, 10, precision);
}

static int _to_dec(char *buf, int32_t value, int fplus, int fspace, int precision)
{
	char *start = buf;

#if (MAXFLD < 10)
  #error buffer size MAXFLD is too small
#endif

	if (value < 0) {
		*buf++ = '-';
		if (value != 0x80000000)
			value = -value;
	} else if (fplus)
		*buf++ = '+';
	else if (fspace)
		*buf++ = ' ';

	return (buf + _to_udec(buf, (uint32_t) value, precision)) - start;
}

static void _llshift(uint32_t value[])
{
	if (value[0] & 0x80000000)
		value[1] = (value[1] << 1) | 1;
	else
		value[1] <<= 1;
	value[0] <<= 1;
}

static void _lrshift(uint32_t value[])
{
	if (value[1] & 1)
		value[0] = (value[0] >> 1) | 0x80000000;
	else
		value[0] = (value[0] >> 1) & 0x7FFFFFFF;
	value[1] = (value[1] >> 1) & 0x7FFFFFFF;
}

static void _ladd(uint32_t result[], uint32_t value[])
{
	uint32_t carry;
	uint32_t temp;

	carry = 0;
	temp = result[0] + value[0];
	if (result[0] & 0x80000000) {
		if ((value[0] & 0x80000000) || ((temp & 0x80000000) == 0))
			carry = 1;
	} else {
		if ((value[0] & 0x80000000) && ((temp & 0x80000000) == 0))
			carry = 1;
	}
	result[0] = temp;
	result[1] = result[1] + value[1] + carry;
}

static	void _rlrshift(uint32_t value[])
{
	uint32_t temp[2];

	temp[0] = value[0] & 1;
	temp[1] = 0;
	_lrshift(value);
	_ladd(value, temp);
}

 /*
  * 64 bit divide by 5 function for _to_float.
  * The result is ROUNDED, not TRUNCATED.
  */

static	void _ldiv5(uint32_t value[])
{
	uint32_t      result[2];
	register int  shift;
	uint32_t      temp1[2];
	uint32_t      temp2[2];

	result[0] = 0;		/* Result accumulator */
	result[1] = value[1] / 5;
	temp1[0] = value[0];	/* Dividend for this pass */
	temp1[1] = value[1] % 5;
	temp2[1] = 0;

	while (1) {
		for (shift = 0; temp1[1] != 0; shift++)
			_lrshift(temp1);
		temp2[0] = temp1[0] / 5;
		if (temp2[0] == 0) {
			if (temp1[0] % 5 > (5 / 2)) {
				temp1[0] = 1;
				_ladd(result, temp1);
			}
			break;
		}
		temp1[0] = temp2[0];
		while (shift-- != 0)
			_llshift(temp1);
		_ladd(result, temp1);	/* Update result accumulator */
		temp1[0] = result[0];
		temp1[1] = result[1];
		_llshift(temp1);	/* Compute (current_result*5) */
		_llshift(temp1);
		_ladd(temp1, result);
		temp1[0] = ~temp1[0];	/* Compute -(current_result*5) */
		temp1[1] = ~temp1[1];
		temp2[0] = 1;
		_ladd(temp1, temp2);
		_ladd(temp1, value);	/* Compute #-(current_result*5) */
	}
	value[0] = result[0];
	value[1] = result[1];
}

static	char _get_digit(uint32_t fract[], int *digit_count)
{
	int		rval;
	uint32_t	temp[2];

	if (*digit_count > 0) {
		*digit_count -= 1;
		temp[0] = fract[0];
		temp[1] = fract[1];
		_llshift(fract);	/* Multiply by 10 */
		_llshift(fract);
		_ladd(fract, temp);
		_llshift(fract);
		rval = ((fract[1] >> 28) & 0xF) + '0';
		fract[1] &= 0x0FFFFFFF;
	} else
		rval = '0';
	return (char) (rval);
}

/*
 *	_to_float
 *
 *	Convert a floating point # to ASCII.
 *
 *	Parameters:
 *		"buf"		Buffer to write result into.
 *		"double_temp"	# to convert (either IEEE single or double).
 *		"c"		The conversion type (one of e,E,f,g,G).
 *		"falt"		TRUE if "#" conversion flag in effect.
 *		"fplus"		TRUE if "+" conversion flag in effect.
 *		"fspace"	TRUE if " " conversion flag in effect.
 *		"precision"	Desired precision (negative if undefined).
 */

/*
 *	The following two constants define the simulated binary floating
 *	point limit for the first stage of the conversion (fraction times
 *	power of two becomes fraction times power of 10), and the second
 *	stage (pulling the resulting decimal digits outs).
 */

#define	MAXFP1	0xFFFFFFFF	/* Largest # if first fp format */
#define	MAXFP2	0x0FFFFFFF	/* Largest # in second fp format */

static int _to_float(char *buf, uint32_t double_temp[], int c,
					 int falt, int fplus, int fspace, int precision)
{
	register int    decexp;
	register int    exp;
	int             digit_count;
	uint32_t        fract[2];
	uint32_t        ltemp[2];
	int             prune_zero;
	char           *start = buf;

	exp = (double_temp[1] >> 20) & 0x7FF;
	fract[1] = (double_temp[1] << 11) & 0x7FFFF800;
	fract[1] |= ((double_temp[0] >> 21) & 0x000007FF);
	fract[0] = double_temp[0] << 11;

	if (exp == 0x7FF) {
		if ((fract[1] | fract[0]) == 0) {
			if ((double_temp[1] & 0x80000000)) {
				*buf++ = '-';
				*buf++ = 'I';
				*buf++ = 'N';
				*buf++ = 'F';
			} else {
				*buf++ = '+';
				*buf++ = 'I';
				*buf++ = 'N';
				*buf++ = 'F';
			}
		} else {
			*buf++ = 'N';
			*buf++ = 'a';
			*buf++ = 'N';
		}
		*buf = 0;
		return buf - start;
	}

	if ((exp | fract[1] | fract[0]) != 0) {
		exp -= (1023 - 1);	/* +1 since .1 vs 1. */
		fract[1] |= 0x80000000;
		decexp = true;		/* Wasn't zero */
	} else
		decexp = false;		/* It was zero */

	if (decexp && (double_temp[1] & 0x80000000)) {
		*buf++ = '-';
	} else if (fplus)
		*buf++ = '+';
	else if (fspace)
		*buf++ = ' ';

	decexp = 0;
	while (exp <= -3) {
		while (fract[1] >= (MAXFP1 / 5)) {
			_rlrshift(fract);
			exp++;
		}
		ltemp[0] = fract[0];
		ltemp[1] = fract[1];
		_llshift(fract);
		_llshift(fract);
		_ladd(fract, ltemp);
		exp++;
		decexp--;

		while (fract[1] <= (MAXFP1 / 2)) {
			_llshift(fract);
			exp--;
		}
	}

	while (exp > 0) {
		_ldiv5(fract);
		exp--;
		decexp++;
		while (fract[1] <= (MAXFP1 / 2)) {
			_llshift(fract);
			exp--;
		}
	}

	while (exp < (0 + 4)) {
		_rlrshift(fract);
		exp++;
	}

	if (precision < 0)
		precision = 6;		/* Default precision if none given */
	prune_zero = false;		/* Assume trailing 0's allowed     */
	if ((c == 'g') || (c == 'G')) {
		if (!falt && (precision > 0))
			prune_zero = true;
		if ((decexp < (-4 + 1)) || (decexp > (precision + 1))) {
			if (c == 'g')
				c = 'e';
			else
				c = 'E';
		} else
			c = 'f';
	}

	if (c == 'f') {
		exp = precision + decexp;
		if (exp < 0)
			exp = 0;
	} else
		exp = precision + 1;
	digit_count = 16;
	if (exp > 16)
		exp = 16;

	ltemp[0] = 0;
	ltemp[1] = 0x08000000;
	while (exp--) {
		_ldiv5(ltemp);
		_rlrshift(ltemp);
	}

	_ladd(fract, ltemp);
	if (fract[1] & 0xF0000000) {
		_ldiv5(fract);
		_rlrshift(fract);
		decexp++;
	}

	if (c == 'f') {
		if (decexp > 0) {
			while (decexp > 0) {
				*buf++ = _get_digit(fract, &digit_count);
				decexp--;
			}
		} else
			*buf++ = '0';
		if (falt || (precision > 0))
			*buf++ = '.';
		while (precision-- > 0) {
			if (decexp < 0) {
				*buf++ = '0';
				decexp++;
			} else
				*buf++ = _get_digit(fract, &digit_count);
		}
	} else {
		*buf = _get_digit(fract, &digit_count);
		if (*buf++ != '0')
			decexp--;
		if (falt || (precision > 0))
			*buf++ = '.';
		while (precision-- > 0)
			*buf++ = _get_digit(fract, &digit_count);
	}

	if (prune_zero) {
		while (*--buf == '0')
			;
		if (*buf != '.')
			buf++;
	}

	if ((c == 'e') || (c == 'E')) {
		*buf++ = (char) c;
		if (decexp < 0) {
			decexp = -decexp;
			*buf++ = '-';
		} else
			*buf++ = '+';
		*buf++ = (char) ((decexp / 100) + '0');
		decexp %= 100;
		*buf++ = (char) ((decexp / 10) + '0');
		decexp %= 10;
		*buf++ = (char) (decexp + '0');
	}
	*buf = 0;

	return buf - start;
}

static int _atoi(char **sptr)
{
	register char *p;
	register int   i;

	i = 0;
	p = *sptr;
	p--;
	while (isdigit(((int) *p)))
		i = 10 * i + *p++ - '0';
	*sptr = p;
	return i;
}

int _prf(int (*func)(), void *dest, char *format, va_list vargs)
{
	/*
	 * Due the fact that buffer is passed to functions in this file,
	 * they assume that it's size if MAXFLD + 1. In need of change
	 * the buffer size, either MAXFLD should be changed or the change
	 * has to be propagated across the file
	 */
	char			buf[MAXFLD + 1];
	register int	c;
	int				count;
	register char	*cptr;
	int				falt;
	int				fminus;
	int				fplus;
	int				fspace;
	register int	i;
	int				need_justifying;
	char			pad;
	int				precision;
	int				prefix;
	int				width;
	char			*cptr_temp;
	int32_t			*int32ptr_temp;
	int32_t			int32_temp;
	uint32_t			uint32_temp;
	uint32_t			double_temp[2];

	count = 0;

	while ((c = *format++)) {
		if (c != '%') {
			if ((*func) (c, dest) == EOF) {
				return EOF;
			}

			count++;

		} else {
			fminus = fplus = fspace = falt = false;
			pad = ' ';		/* Default pad character    */
			precision = -1;	/* No precision specified   */

			while (strchr("-+ #0", (c = *format++)) != NULL) {
				switch (c) {
				case '-':
					fminus = true;
					break;

				case '+':
					fplus = true;
					break;

				case ' ':
					fspace = true;
					break;

				case '#':
					falt = true;
					break;

				case '0':
					pad = '0';
					break;

				case '\0':
					return count;
				}
			}

			if (c == '*') {
				/* Is the width a parameter? */
				width = (int32_t) va_arg(vargs, int32_t);
				if (width < 0) {
					fminus = true;
					width = -width;
				}
				c = *format++;
			} else if (!isdigit(c))
				width = 0;
			else {
				width = _atoi(&format);	/* Find width */
				c = *format++;
			}

			/*
			 * If <width> is INT_MIN, then its absolute value can
			 * not be expressed as a positive number using 32-bit
			 * two's complement.  To cover that case, cast it to
			 * an unsigned before comparing it against MAXFLD.
			 */
			if ((unsigned) width > MAXFLD) {
				width = MAXFLD;
			}

			if (c == '.') {
				c = *format++;
				if (c == '*') {
					precision = (int32_t)
					va_arg(vargs, int32_t);
				} else
					precision = _atoi(&format);

				if (precision > MAXFLD)
					precision = -1;
				c = *format++;
			}

			/*
			 * This implementation only checks that the following format
			 * specifiers are followed by an appropriate type:
			 *    h: short
			 *    l: long
			 *    L: long double
			 *    z: size_t or ssize_t
			 * No further special processing is done for them.
			 */

			if (strchr("hlLz", c) != NULL) {
				i = c;
				c = *format++;
				switch (i) {
				case 'h':
					if (strchr("diouxX", c) == NULL)
						break;
					break;

				case 'l':
					if (strchr("diouxX", c) == NULL)
						break;
					break;

				case 'L':
					if (strchr("eEfgG", c) == NULL)
						break;
					break;

				case 'z':
					if (strchr("diouxX", c) == NULL)
						break;
					break;

				}
			}

			need_justifying = false;
			prefix = 0;
			switch (c) {
			case 'c':
				buf[0] = (char) ((int32_t) va_arg(vargs, int32_t));
				buf[1] = '\0';
				need_justifying = true;
				c = 1;
				break;

			case 'd':
			case 'i':
				int32_temp = (int32_t) va_arg(vargs, int32_t);
				c = _to_dec(buf, int32_temp, fplus, fspace, precision);
				if (fplus || fspace || (int32_temp < 0))
					prefix = 1;
				need_justifying = true;
				if (precision != -1)
					pad = ' ';
				break;

			case 'e':
			case 'E':
			case 'f':
			case 'g':
			case 'G':
				/* standard platforms which supports double */
			{
				union {
					double d;
					struct {
						uint32_t u1;
						uint32_t u2;
						} s;
				} u;

				u.d = (double) va_arg(vargs, double);
#if defined(_BIG_ENDIAN)
				double_temp[0] = u.s.u2;
				double_temp[1] = u.s.u1;
#else
				double_temp[0] = u.s.u1;
				double_temp[1] = u.s.u2;
#endif
			}

				c = _to_float(buf, double_temp, c, falt, fplus,
					      fspace, precision);
				if (fplus || fspace || (buf[0] == '-'))
					prefix = 1;
				need_justifying = true;
				break;

			case 'n':
				int32ptr_temp = (int32_t *)va_arg(vargs, int32_t *);
				*int32ptr_temp = count;
				break;

			case 'o':
				uint32_temp = (uint32_t) va_arg(vargs, uint32_t);
				c = _to_octal(buf, uint32_temp, falt, precision);
				need_justifying = true;
				if (precision != -1)
					pad = ' ';
				break;

			case 'p':
				uint32_temp = (uint32_t) va_arg(vargs, uint32_t);
				c = _to_hex(buf, uint32_temp, true, 8, (int) 'x');
				need_justifying = true;
				if (precision != -1)
					pad = ' ';
				break;

			case 's':
				cptr_temp = (char *) va_arg(vargs, char *);
				/* Get the string length */
				for (c = 0; c < MAXFLD; c++) {
					if (cptr_temp[c] == '\0') {
						break;
					}
				}
				if ((precision >= 0) && (precision < c))
					c = precision;
				if (c > 0) {
					memcpy(buf, cptr_temp, (size_t) c);
					need_justifying = true;
				}
				break;

			case 'u':
				uint32_temp = (uint32_t) va_arg(vargs, uint32_t);
				c = _to_udec(buf, uint32_temp, precision);
				need_justifying = true;
				if (precision != -1)
					pad = ' ';
				break;

			case 'x':
			case 'X':
				uint32_temp = (uint32_t) va_arg(vargs, uint32_t);
				c = _to_hex(buf, uint32_temp, falt, precision, c);
				if (falt)
					prefix = 2;
				need_justifying = true;
				if (precision != -1)
					pad = ' ';
				break;

			case '%':
				if ((*func)('%', dest) == EOF) {
					return EOF;
				}

				count++;
				break;

			case 0:
				return count;
			}

			if (c >= MAXFLD + 1)
				return EOF;

			if (need_justifying) {
				if (c < width) {
					if (fminus)	{
						/* Left justify? */
						for (i = c; i < width; i++)
							buf[i] = ' ';
					} else {
						/* Right justify */
						(void) memmove((buf + (width - c)), buf, (size_t) (c
										+ 1));
						if (pad == ' ')
							prefix = 0;
						c = width - c + prefix;
						for (i = prefix; i < c; i++)
							buf[i] = pad;
					}
					c = width;
				}

				for (cptr = buf; c > 0; c--, cptr++, count++) {
					if ((*func)(*cptr, dest) == EOF)
						return EOF;
				}
			}
		}
	}
	return count;
}
