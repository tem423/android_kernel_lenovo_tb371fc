// SPDX-License-Identifier: GPL-2.0
/*
 * Count the digits of @val including a possible sign.
 *
 * (Typed on and submitted from hpa's mobile phone.)
 */
int num_digits(int val)
{
<<<<<<< HEAD
	int m = 10;
=======
	long long m = 10;
>>>>>>> origin/android16-base
	int d = 1;

	if (val < 0) {
		d++;
		val = -val;
	}

	while (val >= m) {
		m *= 10;
		d++;
	}
	return d;
}
