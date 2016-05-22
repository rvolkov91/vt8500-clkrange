/*
 * Test application for VIA/Wondermedia clock subsystem parameters calculation
 * Copyright (C) 2016 Roman Volkov <rvolkov@v1ros.org>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <limits.h>
#include <time.h>

#define u32 uint32_t

#define VT8500_WM8505_PARENT_RATE	25000000
#define WM8650_PARENT_RATE		25000000
#define LOOP_LIMIT			4000000000 /* Around 4GHz */
#define LOOP_RESOLUTION 		1000 /* 1KHz step */

/*
static void arraylist_add(unsigned int *array, size_t *array_length)
{
	size_t array_size, paged_size;

	array_size = *array_length * sizeof(*array);
	paged_size = array_size & 4096;
	if (array_size >= paged_size)
		array = (unsigned int *)realloc(array, paged_size + 4096);

	(*array_length)++;
}*/

static int vt8500_find_pll_bits_45(unsigned long rate,
	unsigned long parent_rate, u32 *multiplier, u32 *prediv)
{
	unsigned long tclk;

	/* sanity check */
	if ((rate < parent_rate * 4) || (rate > parent_rate * 62)) {
	//	pr_err("%s: requested rate out of range\n", __func__);
		*multiplier = 0;
		*prediv = 1;
		return -EINVAL;
	}
	if (rate <= parent_rate * 31)
		/* use the prediv to double the resolution */
		*prediv = 2;
	else
		*prediv = 1;

	*multiplier = rate / (parent_rate / *prediv);
	tclk = (parent_rate / *prediv) * *multiplier;

	if (tclk != rate)
		return -EINVAL;

	return 0;
}

static int wm8650_find_pll_bits_45(unsigned long rate,
	unsigned long parent_rate, u32 *multiplier, u32 *divisor1,
	u32 *divisor2)
{
	u32 mul, div1;
	int div2;
	u32 best_mul = 0, best_div1 = 0, best_div2 = 0;
	unsigned long tclk, rate_err, best_err, O2_chk;

	best_err = (unsigned long)-1;

	/* Find the closest match (lower or equal to requested) */
	for (div1 = 5; div1 >= 3; div1--)
		for (div2 = 3; div2 >= 0; div2--)
			for (mul = 3; mul <= 1023; mul++) {
				/* Exclude invalid out-of-range cases
				 * 600MHz >= (M * inrate) / P >= 300MHz;
				 * See WMT's GPL kernel source.
				 */
				O2_chk = ((parent_rate * mul) / div1);
				if ((O2_chk < 300000000) || (O2_chk > 600000000))
					continue;
				tclk = parent_rate * mul / (div1 * (1 << div2));
				if (tclk > rate)
					continue;
				/* error will always be +ve */
				rate_err = rate - tclk;
				if (rate_err == 0) {
					*multiplier = mul;
					*divisor1 = div1;
					*divisor2 = div2;
					return 0;
				}

				if (rate_err < best_err) {
					best_err = rate_err;
					best_mul = mul;
					best_div1 = div1;
					best_div2 = div2;
				}
			}

	/* if we got here, it wasn't an exact match */
	//pr_warn("%s: requested rate %lu, found rate %lu\n", __func__, rate,
	//						rate - best_err);
	*multiplier = best_mul;
	*divisor1 = best_div1;
	*divisor2 = best_div2;
	return -EINVAL;
}

/*
 * M * parent [O1] => / P [O2] => / D [O3]
 * Where O1 is 900MHz...3GHz;
 * O2 is 600MHz >= (M * inrate) / P >= 300MHz;
 * M is 36...120 [25MHz parent]; D is 1 or 2 or 4 or 8.
 * Possible ranges (O3):
 * D = 8: 37,5MHz...75MHz
 * D = 4: 75MHz...150MHz
 * D = 2: 150MHz...300MHz
 * D = 1: 300MHz...600MHz
 */
static int wm8650_find_pll_bits_improvement1(unsigned long rate,
	unsigned long parent_rate, u32 *multiplier, u32 *divisor1,
	u32 *divisor2)
{
	unsigned long O1, min_err, rate_err;

	if (!parent_rate || (rate < 37500000) || (rate > 600000000))
		return -EINVAL;

	*divisor2 = rate <= 75000000 ? 3 : rate <= 150000000 ? 2 :
					   rate <= 300000000 ? 1 : 0;
	/*
	 * Divisor P cannot be calculated. Test all divisors and find where M
	 * will be as close as possible to the requested rate.
	 */
	min_err = ULONG_MAX;
	for (*divisor1 = 5; *divisor1 >= 3; (*divisor1)--) {
		O1 = rate * *divisor1 * (1 << (*divisor2));
		rate_err = O1 % parent_rate;
		if (rate_err < min_err) {
			if (rate_err == 0)
				return 0;

			*multiplier = O1 / parent_rate;
			min_err = rate_err;
		}
	}

	//if ((*multiplier < 3) || (*multiplier > 1023))
	//	return -EINVAL;

	//pr_warn("%s: rate error is %lu\n", __func__, min_err);
	return -EINVAL;
}

int main()
{
	unsigned long rates_cnt;
	uint32_t div1, div2, mul1;
	double cputime;
	clock_t begin;

	fprintf(stdout, "VT8500/WM8505 Linux 4.5 range check...\n");
	begin = clock();
	rates_cnt = 0;
	for (unsigned long i = 0; i < LOOP_LIMIT; i += LOOP_RESOLUTION)
	{
		if (!vt8500_find_pll_bits_45(i, VT8500_WM8505_PARENT_RATE,
			&mul1, &div1))
		{
			fprintf(stdout, rates_cnt ? ",%lu" : "%lu", i);
			rates_cnt++;
		}
	}

	cputime = (double)(clock() - begin) / CLOCKS_PER_SEC;
	fprintf(stdout, "\n%lu rates found, CPU spent %.2fs\n",
		rates_cnt, cputime);

	fprintf(stdout, "WM8650 Linux 4.5 range check...\n");
	begin = clock();
	rates_cnt = 0;
	for (unsigned long i = 0; i < LOOP_LIMIT; i += LOOP_RESOLUTION)
	{
		if (!wm8650_find_pll_bits_45(i, WM8650_PARENT_RATE,
			&mul1, &div1, &div2))
		{
			fprintf(stdout, rates_cnt ? ",%lu" : "%lu", i);
			rates_cnt++;
		}
	}

	cputime = (double)(clock() - begin) / CLOCKS_PER_SEC;
	fprintf(stdout, "\n%lu rates found, CPU spent %.2fs\n",
		rates_cnt, cputime);

	fprintf(stdout, "WM8650 improvement 1 range check...\n");
	begin = clock();
	rates_cnt = 0;
	for (unsigned long i = 0; i < LOOP_LIMIT; i += LOOP_RESOLUTION)
	{
		if (!wm8650_find_pll_bits_improvement1(i, WM8650_PARENT_RATE,
			&mul1, &div1, &div2))
		{
			fprintf(stdout, rates_cnt ? ",%lu" : "%lu", i);
			rates_cnt++;
		}
	}

	cputime = (double)(clock() - begin) / CLOCKS_PER_SEC;
	fprintf(stdout, "\n%lu rates found, CPU spent %.2fs\n",
		rates_cnt, cputime);
}
